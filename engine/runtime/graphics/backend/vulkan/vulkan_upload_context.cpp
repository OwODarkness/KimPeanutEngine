#include "vulkan_upload_context.h"

#include <stdexcept>

#include "log/logger.h"
#include "vulkan_buffer_manager.h"
#include "vulkan_device.h"
#include "vulkan_frame_context.h"

namespace kpengine::graphics
{
#define KP_VULKAN_UPLOAD_CONTEXT_LOG_NAME "VulkanUploadContextLog"

    void VulkanUploadContext::Initialize(VulkanDevice *device, VulkanFrameContext *frame_context,
                                         VulkanBufferManager *buffer_manager)
    {
        device_ = device;
        frame_context_ = frame_context;
        buffer_manager_ = buffer_manager;
    }

    BufferHandle VulkanUploadContext::CreateUploadStageBuffer(size_t size)
    {
        VkBufferCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        create_info.size = size;
        create_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        return buffer_manager_->CreateBufferResource(
            device_->GetLogicalDevice(), &create_info, VulkanMemoryUsageType::MEMORY_USAGE_STAGING);
    }

    VkCommandBuffer VulkanUploadContext::BeginOneShot(VkCommandPool command_pool)
    {
        VkCommandBufferAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocate_info.commandPool = command_pool;
        allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocate_info.commandBufferCount = 1;

        VkCommandBuffer command_buffer = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(device_->GetLogicalDevice(), &allocate_info, &command_buffer) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate Vulkan upload command buffer");
        }

        VkCommandBufferBeginInfo begin_info{};
        begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS)
        {
            vkFreeCommandBuffers(device_->GetLogicalDevice(), command_pool, 1, &command_buffer);
            throw std::runtime_error("failed to begin Vulkan upload command buffer");
        }
        return command_buffer;
    }

    void VulkanUploadContext::SubmitAndRelease(VkCommandBuffer command_buffer,
                                                VkCommandPool command_pool, VkQueue queue)
    {
        if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS)
        {
            vkFreeCommandBuffers(device_->GetLogicalDevice(), command_pool, 1, &command_buffer);
            throw std::runtime_error("failed to end Vulkan upload command buffer");
        }

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer;
        if (vkQueueSubmit(queue, 1, &submit_info, VK_NULL_HANDLE) != VK_SUCCESS)
        {
            vkFreeCommandBuffers(device_->GetLogicalDevice(), command_pool, 1, &command_buffer);
            throw std::runtime_error("failed to submit Vulkan upload command buffer");
        }
        if (vkQueueWaitIdle(queue) != VK_SUCCESS)
        {
            vkFreeCommandBuffers(device_->GetLogicalDevice(), command_pool, 1, &command_buffer);
            throw std::runtime_error("failed waiting for Vulkan upload queue");
        }
        vkFreeCommandBuffers(device_->GetLogicalDevice(), command_pool, 1, &command_buffer);
    }

    void VulkanUploadContext::UploadBuffer(BufferHandle destination, size_t size, const void *data)
    {
        if (!device_ || !frame_context_ || !buffer_manager_ || !data || size == 0)
        {
            throw std::runtime_error("Vulkan upload context is not ready for a buffer upload");
        }

        const BufferHandle staging = CreateUploadStageBuffer(size);
        try
        {
            buffer_manager_->UploadData(staging, size, data);
            VulkanBufferResource *source_resource = buffer_manager_->GetBufferResource(staging);
            VulkanBufferResource *destination_resource = buffer_manager_->GetBufferResource(destination);
            if (!source_resource || !destination_resource)
            {
                throw std::runtime_error("invalid Vulkan buffer in upload");
            }

            VkCommandBuffer command_buffer = BeginOneShot(frame_context_->GetTransferCommandPool());
            VkBufferCopy copy{};
            copy.size = size;
            vkCmdCopyBuffer(command_buffer, source_resource->buffer, destination_resource->buffer, 1, &copy);
            SubmitAndRelease(command_buffer, frame_context_->GetTransferCommandPool(),
                             device_->GetTransferQueue().queue);
        }
        catch (...)
        {
            buffer_manager_->DestroyBufferResource(device_->GetLogicalDevice(), staging);
            throw;
        }
        buffer_manager_->DestroyBufferResource(device_->GetLogicalDevice(), staging);
    }

    void VulkanUploadContext::UploadTexture(VkImage image, const void *pixels, size_t pixel_size,
                                            uint32_t width, uint32_t height, uint32_t mip_levels)
    {
        if (!device_ || !frame_context_ || !buffer_manager_ || image == VK_NULL_HANDLE ||
            !pixels || pixel_size == 0 || width == 0 || height == 0 || mip_levels == 0)
        {
            throw std::runtime_error("invalid Vulkan texture upload");
        }

        const BufferHandle staging = CreateUploadStageBuffer(pixel_size);
        try
        {
            buffer_manager_->UploadData(staging, pixel_size, pixels);
            VulkanBufferResource *source_resource = buffer_manager_->GetBufferResource(staging);
            if (!source_resource)
            {
                throw std::runtime_error("invalid Vulkan texture staging buffer");
            }

            VkCommandBuffer command_buffer = BeginOneShot(frame_context_->GetGraphicsCommandPool());
            frame_context_->TransitionImageLayout(command_buffer, image, TextureUsage::None,
                                                  TextureUsage::TEXTURE_USAGE_TRANSFER_DST, 0, mip_levels);
            frame_context_->CopyBufferToImage(command_buffer, source_resource->buffer, image, width, height);
            frame_context_->TransitionImageLayout(command_buffer, image,
                                                  TextureUsage::TEXTURE_USAGE_TRANSFER_DST,
                                                  TextureUsage::TEXTURE_USAGE_SAMPLE, 0, mip_levels);
            SubmitAndRelease(command_buffer, frame_context_->GetGraphicsCommandPool(),
                             device_->GetGraphicsQueue().queue);
        }
        catch (...)
        {
            buffer_manager_->DestroyBufferResource(device_->GetLogicalDevice(), staging);
            throw;
        }
        buffer_manager_->DestroyBufferResource(device_->GetLogicalDevice(), staging);
    }
}
