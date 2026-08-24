#include "vulkan_editor_bridge.h"

#include <stdexcept>

#include "vulkan_device.h"
#include "vulkan_frame_context.h"
#include "vulkan_swapchain.h"

namespace kpengine::graphics
{
    VulkanEditorBridge::VulkanEditorBridge(VulkanDevice &device, VulkanSwapchain &swapchain,
                                           VulkanFrameContext &frame_context)
        : device_(&device), swapchain_(&swapchain), frame_context_(&frame_context)
    {
        OnSwapchainRecreated();
    }

    VulkanEditorBridgeInfo VulkanEditorBridge::GetInfo() const
    {
        const VulkanQueue &graphics_queue = device_->GetGraphicsQueue();
        return {device_->GetInstance(), device_->GetPhysicalDevice(), device_->GetLogicalDevice(),
                graphics_queue.queue, graphics_queue.index,
                static_cast<uint32_t>(swapchain_->GetImageCount()), swapchain_->GetImageFormat()};
    }

    void VulkanEditorBridge::BeginFrame(uint32_t image_index)
    {
        current_image_index_ = image_index;
        frame_active_ = image_index < image_layouts_.size();
    }

    void VulkanEditorBridge::EndFrame()
    {
        frame_active_ = false;
    }

    bool VulkanEditorBridge::Record(const std::function<void(VkCommandBuffer)> &record_draw_data)
    {
        if (!frame_active_ || !record_draw_data)
        {
            return false;
        }

        const VkCommandBuffer command_buffer = frame_context_->GetCurrentSceneCommandBuffer();
        TransitionToColorAttachment(command_buffer);
        try
        {
            record_draw_data(command_buffer);
        }
        catch (...)
        {
            vkCmdEndRendering(command_buffer);
            TransitionToPresent(command_buffer);
            frame_active_ = false;
            throw;
        }
        vkCmdEndRendering(command_buffer);
        TransitionToPresent(command_buffer);
        frame_active_ = false;
        return true;
    }

    void VulkanEditorBridge::EnsurePresentLayout(VkCommandBuffer command_buffer,
                                                 uint32_t image_index)
    {
        if (command_buffer == VK_NULL_HANDLE || image_index >= image_layouts_.size() ||
            image_layouts_[image_index] == VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)
        {
            return;
        }

        const VkImageLayout old_layout = image_layouts_[image_index];
        frame_context_->TransitionImageLayout(
            command_buffer, swapchain_->GetImage(image_index), old_layout,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT,
            old_layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_2_MEMORY_READ_BIT, 0,
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
        image_layouts_[image_index] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }

    void VulkanEditorBridge::OnSwapchainRecreated()
    {
        image_layouts_.assign(swapchain_->GetImageCount(), VK_IMAGE_LAYOUT_UNDEFINED);
        frame_active_ = false;
    }

    void VulkanEditorBridge::WaitIdle() const
    {
        vkDeviceWaitIdle(device_->GetLogicalDevice());
    }

    void VulkanEditorBridge::TransitionToColorAttachment(VkCommandBuffer command_buffer)
    {
        const VkImageLayout old_layout = image_layouts_[current_image_index_];
        frame_context_->TransitionImageLayout(
            command_buffer, swapchain_->GetImage(current_image_index_), old_layout,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            old_layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 : VK_ACCESS_2_MEMORY_READ_BIT,
            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);

        VkClearValue clear_value{};
        clear_value.color = {{0.1f, 0.1f, 0.1f, 1.0f}};
        VkRenderingAttachmentInfo color_attachment{};
        color_attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color_attachment.imageView = swapchain_->GetImageView(current_image_index_);
        color_attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        color_attachment.clearValue = clear_value;

        VkRenderingInfo rendering_info{};
        rendering_info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering_info.renderArea.extent = swapchain_->GetExtent();
        rendering_info.layerCount = 1;
        rendering_info.colorAttachmentCount = 1;
        rendering_info.pColorAttachments = &color_attachment;
        vkCmdBeginRendering(command_buffer, &rendering_info);
        image_layouts_[current_image_index_] = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }

    void VulkanEditorBridge::TransitionToPresent(VkCommandBuffer command_buffer)
    {
        frame_context_->TransitionImageLayout(
            command_buffer, swapchain_->GetImage(current_image_index_),
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 0,
            VK_IMAGE_ASPECT_COLOR_BIT, 0, 1);
        image_layouts_[current_image_index_] = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
}
