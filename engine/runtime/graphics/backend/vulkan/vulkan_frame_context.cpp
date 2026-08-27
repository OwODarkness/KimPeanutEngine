#include "vulkan_frame_context.h"

#include <algorithm>
#include <array>

#include "log/logger.h"

namespace kpengine::graphics
{
#define KP_VULKAN_FRAME_CONTEXT_LOG_NAME "VulkanFrameContextLog"

    void VulkanFrameContext::Initialize(VulkanDevice *device, uint32_t swapchain_image_count)
    {
        device_ = device;
        swapchain_image_count_ = swapchain_image_count;
        CreateCommandPools();
        CreateCommandBuffers();
        CreateSyncObjects();
    }

    void VulkanFrameContext::Destroy()
    {
        for (VkSemaphore &semaphore : available_image_semaphores_)
        {
            vkDestroySemaphore(device_->GetLogicalDevice(), semaphore, nullptr);
        }
        for (VkSemaphore &semaphore : render_finished_semaphores_)
        {
            vkDestroySemaphore(device_->GetLogicalDevice(), semaphore, nullptr);
        }
        for (VkFence &fence : in_flight_fences_)
        {
            vkDestroyFence(device_->GetLogicalDevice(), fence, nullptr);
        }
        vkDestroyCommandPool(device_->GetLogicalDevice(), graphics_command_pool_, nullptr);
        vkDestroyCommandPool(device_->GetLogicalDevice(), transfer_command_pool_, nullptr);
    }

    void VulkanFrameContext::WaitForInFlightFence()
    {
        vkWaitForFences(device_->GetLogicalDevice(), 1, &in_flight_fences_[current_frame_index_], VK_TRUE, UINT64_MAX);
        completed_submission_serial_ = std::max(completed_submission_serial_,
                                                in_flight_submission_serials_[current_frame_index_]);
    }

    VkResult VulkanFrameContext::AcquireNextImage(VkSwapchainKHR swapchain, uint32_t &image_index)
    {
        return vkAcquireNextImageKHR(device_->GetLogicalDevice(), swapchain, UINT64_MAX, available_image_semaphores_[current_frame_index_], VK_NULL_HANDLE, &image_index);
    }

    void VulkanFrameContext::ResetInFlightFence()
    {
        vkResetFences(device_->GetLogicalDevice(), 1, &in_flight_fences_[current_frame_index_]);
    }

    void VulkanFrameContext::ResetCurrentSceneCommandBuffer()
    {
        vkResetCommandBuffer(scene_command_buffers_[current_frame_index_], 0);
    }

    void VulkanFrameContext::Submit(VkCommandBuffer command_buffer, uint32_t image_index)
    {
        size_t render_finished_index = swapchain_image_count_ * current_frame_index_ + image_index;

        std::array<VkSemaphore, 1> wait_semaphores = {available_image_semaphores_[current_frame_index_]};
        std::array<VkPipelineStageFlags, 1> wait_stages = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
        std::array<VkSemaphore, 1> signal_semaphores = {render_finished_semaphores_[render_finished_index]};

        VkSubmitInfo submit_info{};
        submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit_info.commandBufferCount = 1;
        submit_info.pCommandBuffers = &command_buffer;
        submit_info.waitSemaphoreCount = static_cast<uint32_t>(wait_semaphores.size());
        submit_info.pWaitSemaphores = wait_semaphores.data();
        submit_info.signalSemaphoreCount = static_cast<uint32_t>(signal_semaphores.size());
        submit_info.pSignalSemaphores = signal_semaphores.data();
        submit_info.pWaitDstStageMask = wait_stages.data();

        if (vkQueueSubmit(device_->GetGraphicsQueue().queue, 1, &submit_info, in_flight_fences_[current_frame_index_]) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_FRAME_CONTEXT_LOG_NAME, LOG_LEVEL_ERROR, "Failed to submit commandbuffer");
            throw std::runtime_error("Failed to submit commandbuffer");
        }
        in_flight_submission_serials_[current_frame_index_] = next_submission_serial_;
        last_submitted_serial_ = next_submission_serial_;
        ++next_submission_serial_;
    }

    VkResult VulkanFrameContext::Present(VkSwapchainKHR swapchain, uint32_t image_index)
    {
        size_t render_finished_index = swapchain_image_count_ * current_frame_index_ + image_index;
        std::array<VkSemaphore, 1> signal_semaphores = {render_finished_semaphores_[render_finished_index]};

        VkPresentInfoKHR present_info{};
        present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present_info.pImageIndices = &image_index;
        present_info.swapchainCount = 1;
        present_info.pSwapchains = &swapchain;
        present_info.waitSemaphoreCount = static_cast<uint32_t>(signal_semaphores.size());
        present_info.pWaitSemaphores = signal_semaphores.data();
        present_info.pResults = nullptr;

        return vkQueuePresentKHR(device_->GetPresentQueue().queue, &present_info);
    }

    void VulkanFrameContext::OnSwapchainRecreated(uint32_t swapchain_image_count)
    {
        // render_finished_semaphores_ is indexed by frame * image_count; a
        // swapchain recreate can change the image count, so the vector is rebuilt
        // instead of the old count silently indexing past its end.
        swapchain_image_count_ = swapchain_image_count;
        for (VkSemaphore &semaphore : render_finished_semaphores_)
        {
            vkDestroySemaphore(device_->GetLogicalDevice(), semaphore, nullptr);
        }
        render_finished_semaphores_.resize(MAX_FRAMES_IN_FLIGHT * swapchain_image_count_);

        VkSemaphoreCreateInfo semaphore_create_info{};
        semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        for (VkSemaphore &semaphore : render_finished_semaphores_)
        {
            if (vkCreateSemaphore(device_->GetLogicalDevice(), &semaphore_create_info, nullptr, &semaphore) != VK_SUCCESS)
            {
                KP_LOG(KP_VULKAN_FRAME_CONTEXT_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create render_finished_semaphore");
                throw std::runtime_error("Failed to create render_finished_semaphores");
            }
        }
    }

    void VulkanFrameContext::TransitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout, VkPipelineStageFlags2 src_stage, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 src_access, VkAccessFlags2 dst_access, VkImageAspectFlags aspect_mask, uint32_t base_mip_level, uint32_t level_count)
    {
        VkImageMemoryBarrier2 barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
        barrier.image = image;
        barrier.oldLayout = old_layout;
        barrier.newLayout = new_layout;
        barrier.srcStageMask = src_stage;
        barrier.dstStageMask = dst_stage;
        barrier.srcAccessMask = src_access;
        barrier.dstAccessMask = dst_access;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;

        VkImageSubresourceRange subresource_range{};
        subresource_range.aspectMask = aspect_mask;
        subresource_range.baseMipLevel = base_mip_level;
        subresource_range.levelCount = level_count;
        subresource_range.baseArrayLayer = 0;
        subresource_range.layerCount = 1;
        barrier.subresourceRange = subresource_range;

        VkDependencyInfo depend_info{};
        depend_info.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
        depend_info.dependencyFlags = {};
        depend_info.imageMemoryBarrierCount = 1;
        depend_info.pImageMemoryBarriers = &barrier;

        vkCmdPipelineBarrier2(cmd, &depend_info);
    }

    void VulkanFrameContext::TransitionImageLayout(VkCommandBuffer cmd, VkImage image, TextureUsage src_usage, TextureUsage dst_usage, uint32_t base_mip_level, uint32_t level_count)
    {
        if (src_usage == TextureUsage::None && dst_usage == TextureUsage::TEXTURE_USAGE_TRANSFER_DST)
        {
            TransitionImageLayout(
                cmd, image,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                0, VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT, base_mip_level, level_count);
        }
        else if (src_usage == TextureUsage::TEXTURE_USAGE_TRANSFER_DST && dst_usage == TextureUsage::TEXTURE_USAGE_SAMPLE)
        {
            TransitionImageLayout(
                cmd, image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_ACCESS_2_SHADER_READ_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT, base_mip_level, level_count);
        }
        else if (src_usage == TextureUsage::None && dst_usage == TextureUsage::TEXTURE_USAGE_DEPTHSTENCIL_ATTACHMENT)
        {
            TransitionImageLayout(
                cmd, image,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT,
                0, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_ASPECT_DEPTH_BIT, base_mip_level, level_count);
        }
        else if (src_usage == TextureUsage::None && dst_usage == TextureUsage::TEXTURE_USAGE_COLOR_ATTACHMENT)
        {
            TransitionImageLayout(
                cmd, image,
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                0, VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT, base_mip_level, level_count);
        }
        else
        {
            KP_LOG(KP_VULKAN_FRAME_CONTEXT_LOG_NAME, LOG_LEVEL_ERROR, "Unsupport layout transition");
            throw std::runtime_error("Unsupport layout transition");
        }
    }

    void VulkanFrameContext::CopyBufferToImage(VkCommandBuffer cmd, VkBuffer src_buffer, VkImage image, uint32_t width, uint32_t height)
    {
        VkBufferImageCopy region{};
        region.bufferRowLength = 0;
        region.bufferOffset = 0;
        region.bufferImageHeight = 0;
        region.imageOffset = {0, 0, 0};
        region.imageExtent = {width, height, 1};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;

        vkCmdCopyBufferToImage(cmd, src_buffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
    }

    void VulkanFrameContext::CreateCommandPools()
    {
        // graphics pool create
        VkCommandPoolCreateInfo graphics_command_pool_create_info{};
        graphics_command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        graphics_command_pool_create_info.queueFamilyIndex = device_->GetGraphicsQueue().index;
        graphics_command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(device_->GetLogicalDevice(), &graphics_command_pool_create_info, nullptr, &graphics_command_pool_) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_FRAME_CONTEXT_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create graphics command pool");
            throw std::runtime_error("Failed to create graphics command pool");
        }

        // transfer pool create
        VkCommandPoolCreateInfo transfer_command_pool_create_info{};
        transfer_command_pool_create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        transfer_command_pool_create_info.queueFamilyIndex = device_->GetTransferQueue().index;
        transfer_command_pool_create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        if (vkCreateCommandPool(device_->GetLogicalDevice(), &transfer_command_pool_create_info, nullptr, &transfer_command_pool_) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_FRAME_CONTEXT_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create transfer command pool");
            throw std::runtime_error("Failed to create transfer command pool");
        }
    }

    void VulkanFrameContext::CreateCommandBuffers()
    {
        VkCommandBufferAllocateInfo scene_allocate_info{};
        scene_allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        scene_allocate_info.commandBufferCount = MAX_FRAMES_IN_FLIGHT;
        scene_allocate_info.commandPool = graphics_command_pool_;
        scene_allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

        scene_command_buffers_.resize(MAX_FRAMES_IN_FLIGHT);
        if (vkAllocateCommandBuffers(device_->GetLogicalDevice(), &scene_allocate_info, scene_command_buffers_.data()) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_FRAME_CONTEXT_LOG_NAME, LOG_LEVEL_ERROR, "Failed to allocate scene command buffer");
            throw std::runtime_error("Failed to allocate scene command buffer");
        }

    }

    void VulkanFrameContext::CreateSyncObjects()
    {
        VkSemaphoreCreateInfo semaphore_create_info{};
        semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fence_create_info{};
        fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        available_image_semaphores_.resize(MAX_FRAMES_IN_FLIGHT);
        for (size_t i = 0; i < available_image_semaphores_.size(); i++)
        {
            if (vkCreateSemaphore(device_->GetLogicalDevice(), &semaphore_create_info, nullptr, &available_image_semaphores_[i]) != VK_SUCCESS)
            {
                KP_LOG(KP_VULKAN_FRAME_CONTEXT_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create available_image_semaphore");
                throw std::runtime_error("Failed to create available_image_semaphore");
            }
        }

        in_flight_fences_.resize(MAX_FRAMES_IN_FLIGHT);
        in_flight_submission_serials_.assign(MAX_FRAMES_IN_FLIGHT, 0);
        for (size_t i = 0; i < in_flight_fences_.size(); i++)
        {
            if (vkCreateFence(device_->GetLogicalDevice(), &fence_create_info, nullptr, &in_flight_fences_[i]) != VK_SUCCESS)
            {
                KP_LOG(KP_VULKAN_FRAME_CONTEXT_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create in flight fence");
                throw std::runtime_error("Failed to create in_flight_fence");
            }
        }

        render_finished_semaphores_.resize(MAX_FRAMES_IN_FLIGHT * swapchain_image_count_);
        for (size_t i = 0; i < render_finished_semaphores_.size(); i++)
        {
            if (vkCreateSemaphore(device_->GetLogicalDevice(), &semaphore_create_info, nullptr, &render_finished_semaphores_[i]) != VK_SUCCESS)
            {
                KP_LOG(KP_VULKAN_FRAME_CONTEXT_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create render_finished_semaphore");
                throw std::runtime_error("Failed to create render_finished_semaphores");
            }
        }
    }
}
