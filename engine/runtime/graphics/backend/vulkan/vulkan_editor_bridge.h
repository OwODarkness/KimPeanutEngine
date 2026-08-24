#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_EDITOR_BRIDGE_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_EDITOR_BRIDGE_H

#include <cstdint>
#include <functional>
#include <vector>

#include <vulkan/vulkan.h>

namespace kpengine::graphics
{
    class VulkanDevice;
    class VulkanFrameContext;
    class VulkanSwapchain;

    struct VulkanEditorBridgeInfo
    {
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physical_device = VK_NULL_HANDLE;
        VkDevice logical_device = VK_NULL_HANDLE;
        VkQueue graphics_queue = VK_NULL_HANDLE;
        uint32_t graphics_queue_family = 0;
        uint32_t image_count = 0;
        VkFormat color_format = VK_FORMAT_UNDEFINED;
    };

    // A frame-scoped external-pass capability for the editor. It borrows all
    // Vulkan resources from the backend and never exposes ownership to the editor.
    class VulkanEditorBridge final
    {
    public:
        VulkanEditorBridge(VulkanDevice &device, VulkanSwapchain &swapchain,
                           VulkanFrameContext &frame_context);

        VulkanEditorBridgeInfo GetInfo() const;
        void BeginFrame(uint32_t image_index);
        void EndFrame();
        bool Record(const std::function<void(VkCommandBuffer)> &record_draw_data);
        void EnsurePresentLayout(VkCommandBuffer command_buffer, uint32_t image_index);
        void OnSwapchainRecreated();
        void WaitIdle() const;

    private:
        void TransitionToColorAttachment(VkCommandBuffer command_buffer);
        void TransitionToPresent(VkCommandBuffer command_buffer);

        VulkanDevice *device_ = nullptr;
        VulkanSwapchain *swapchain_ = nullptr;
        VulkanFrameContext *frame_context_ = nullptr;
        std::vector<VkImageLayout> image_layouts_;
        uint32_t current_image_index_ = 0;
        bool frame_active_ = false;
    };
}

#endif
