#include "vulkan_render_target_readback.h"

#include <cstring>
#include <iterator>
#include <limits>
#include <utility>

#include "vulkan_buffer_manager.h"
#include "vulkan_frame_context.h"
#include "vulkan_render_target_manager.h"

namespace kpengine::graphics
{
    namespace
    {
        size_t GetByteCount(uint32_t width, uint32_t height)
        {
            constexpr size_t kBytesPerPixel = 4;
            if (width == 0 || height == 0 ||
                static_cast<size_t>(width) > std::numeric_limits<size_t>::max() / height)
            {
                return 0;
            }
            const size_t pixel_count = static_cast<size_t>(width) * height;
            return pixel_count <= std::numeric_limits<size_t>::max() / kBytesPerPixel
                       ? pixel_count * kBytesPerPixel
                       : 0;
        }
    }

    VulkanRenderTargetReadback::VulkanRenderTargetReadback(
        VkDevice logical_device, VulkanFrameContext &frame_context,
        VulkanBufferManager &buffer_manager, VulkanRenderTargetManager &render_target_manager)
        : logical_device_(logical_device), frame_context_(&frame_context),
          buffer_manager_(&buffer_manager), render_target_manager_(&render_target_manager)
    {
    }

    bool VulkanRenderTargetReadback::EnqueueRenderTargetReadback(
        RenderTargetReadbackRequest request, RenderTargetReadbackCallback on_completed)
    {
        if (!on_completed)
        {
            return false;
        }
        if (!request.IsValid() || !render_target_manager_->CanReadback(request.target))
        {
            on_completed({RenderTargetReadbackStatus::InvalidTarget, {},
                          "Vulkan render-target readback source is invalid"});
            return true;
        }

        std::scoped_lock lock(mutex_);
        queued_.push_back({request, std::move(on_completed), {}, 0});
        return true;
    }

    void VulkanRenderTargetReadback::RecordPendingCopies(VkCommandBuffer command_buffer,
                                                          uint64_t submission_serial)
    {
        if (command_buffer == VK_NULL_HANDLE || submission_serial == 0)
        {
            return;
        }

        std::vector<PendingReadback> pending;
        {
            std::scoped_lock lock(mutex_);
            pending.swap(queued_);
        }

        std::vector<PendingReadback> failed;
        for (PendingReadback &entry : pending)
        {
            VulkanRenderTargetManager::ReadbackSource source{};
            if (!render_target_manager_->GetReadbackSource(entry.request.target, source))
            {
                failed.push_back(std::move(entry));
                continue;
            }
            const size_t byte_count = GetByteCount(source.width, source.height);
            if (byte_count == 0 || byte_count > std::numeric_limits<VkDeviceSize>::max())
            {
                failed.push_back(std::move(entry));
                continue;
            }

            VkBufferCreateInfo buffer_info{};
            buffer_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            buffer_info.size = static_cast<VkDeviceSize>(byte_count);
            buffer_info.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            buffer_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            try
            {
                entry.staging_buffer = buffer_manager_->CreateBufferResource(
                    logical_device_, &buffer_info, VulkanMemoryUsageType::MEMORY_USAGE_STAGING);
            }
            catch (...)
            {
                failed.push_back(std::move(entry));
                continue;
            }

            VulkanBufferResource *const staging = buffer_manager_->GetBufferResource(entry.staging_buffer);
            if (staging == nullptr)
            {
                failed.push_back(std::move(entry));
                continue;
            }
            frame_context_->TransitionImageLayout(
                command_buffer, source.image, source.layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_MEMORY_READ_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
            VkBufferImageCopy copy{};
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            copy.imageSubresource.layerCount = 1;
            copy.imageExtent = {source.width, source.height, 1};
            vkCmdCopyImageToBuffer(command_buffer, source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                                   staging->buffer, 1, &copy);
            frame_context_->TransitionImageLayout(
                command_buffer, source.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, source.layout,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                VK_ACCESS_2_TRANSFER_READ_BIT, VK_ACCESS_2_MEMORY_READ_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
            entry.submission_serial = submission_serial;
            std::scoped_lock lock(mutex_);
            submitted_.push_back(std::move(entry));
        }

        for (PendingReadback &entry : failed)
        {
            if (entry.staging_buffer.IsValid())
            {
                buffer_manager_->DestroyBufferResource(logical_device_, entry.staging_buffer);
            }
            entry.on_completed({RenderTargetReadbackStatus::Failed, {},
                                "Vulkan could not allocate or record the render-target readback"});
        }
    }

    void VulkanRenderTargetReadback::CollectCompleted(uint64_t completed_submission_serial)
    {
        std::vector<PendingReadback> completed;
        {
            std::scoped_lock lock(mutex_);
            auto first_pending = submitted_.begin();
            for (auto current = submitted_.begin(); current != submitted_.end(); ++current)
            {
                if (current->submission_serial <= completed_submission_serial)
                {
                    completed.push_back(std::move(*current));
                }
                else
                {
                    *first_pending++ = std::move(*current);
                }
            }
            submitted_.erase(first_pending, submitted_.end());
        }

        for (PendingReadback &entry : completed)
        {
            VulkanRenderTargetManager::ReadbackSource source{};
            const bool has_source = render_target_manager_->GetReadbackSource(entry.request.target, source);
            const size_t byte_count = has_source ? GetByteCount(source.width, source.height) : 0;
            void *const mapped = byte_count == 0 ? nullptr :
                buffer_manager_->GetMappedAddress(entry.staging_buffer, static_cast<VkDeviceSize>(byte_count));
            if (mapped != nullptr)
            {
                CapturedImage image{};
                image.width = source.width;
                image.height = source.height;
                image.frame_number = entry.request.frame_number;
                image.submission_serial = entry.submission_serial;
                image.rgba8_pixels.resize(byte_count);
                std::memcpy(image.rgba8_pixels.data(), mapped, byte_count);
                buffer_manager_->DestroyBufferResource(logical_device_, entry.staging_buffer);
                entry.on_completed({RenderTargetReadbackStatus::Captured, std::move(image), {}});
            }
            else
            {
                buffer_manager_->DestroyBufferResource(logical_device_, entry.staging_buffer);
                entry.on_completed({RenderTargetReadbackStatus::Failed, {},
                                    "Vulkan readback staging memory was unavailable"});
            }
        }
    }

    void VulkanRenderTargetReadback::CollectCompletedReadbacks()
    {
        if (frame_context_)
        {
            CollectCompleted(frame_context_->GetCompletedSubmissionSerial());
        }
    }

    void VulkanRenderTargetReadback::CancelTarget(RenderTargetHandle target, std::string diagnostic)
    {
        std::vector<PendingReadback> cancelled;
        {
            std::scoped_lock lock(mutex_);
            const auto move_target = [&cancelled, target](std::vector<PendingReadback> &entries)
            {
                auto first_remaining = entries.begin();
                for (auto current = entries.begin(); current != entries.end(); ++current)
                {
                    if (current->request.target == target)
                    {
                        cancelled.push_back(std::move(*current));
                    }
                    else
                    {
                        *first_remaining++ = std::move(*current);
                    }
                }
                entries.erase(first_remaining, entries.end());
            };
            move_target(queued_);
            move_target(submitted_);
        }
        CompleteCancelled(std::move(cancelled), diagnostic);
    }

    void VulkanRenderTargetReadback::DrainPendingReadbacks(std::string diagnostic)
    {
        std::vector<PendingReadback> cancelled;
        {
            std::scoped_lock lock(mutex_);
            cancelled.swap(queued_);
            cancelled.insert(cancelled.end(), std::make_move_iterator(submitted_.begin()),
                             std::make_move_iterator(submitted_.end()));
            submitted_.clear();
        }
        CompleteCancelled(std::move(cancelled), diagnostic);
    }

    void VulkanRenderTargetReadback::CompleteCancelled(std::vector<PendingReadback> pending,
                                                        const std::string &diagnostic)
    {
        for (PendingReadback &entry : pending)
        {
            if (entry.staging_buffer.IsValid())
            {
                buffer_manager_->DestroyBufferResource(logical_device_, entry.staging_buffer);
            }
            entry.on_completed({RenderTargetReadbackStatus::Cancelled, {}, diagnostic});
        }
    }
}
