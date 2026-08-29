#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_RENDER_TARGET_MANAGER_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_RENDER_TARGET_MANAGER_H

#include <vector>

#include <vulkan/vulkan.h>

#include "base/handle.h"
#include "common/render_target.h"

namespace kpengine::graphics
{
    class TextureManager;
    class VulkanFrameContext;

    // Owns Vulkan render-target resources and their persistent image-layout state.
    // Recording code selects a handle; it never owns its images or transitions.
    class VulkanRenderTargetManager final
    {
    public:
        struct ReadbackSource
        {
            VkImage image = VK_NULL_HANDLE;
            VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
            uint32_t width = 0;
            uint32_t height = 0;
        };

        VulkanRenderTargetManager(VkDevice logical_device, GraphicsContext context,
                                  VulkanFrameContext &frame_context,
                                  TextureManager &texture_manager);

        RenderTargetHandle Create(const RenderTargetDesc &desc);
        bool Destroy(RenderTargetHandle handle);
        TextureHandle GetColor(RenderTargetHandle handle) const;
        RenderTargetView GetView(RenderTargetHandle handle) const;
        bool CanReadback(RenderTargetHandle handle) const;
        bool GetReadbackSource(RenderTargetHandle handle, ReadbackSource &out_source) const;

        bool BeginRendering(VkCommandBuffer command_buffer, RenderTargetHandle handle);
        void EndRendering(VkCommandBuffer command_buffer);

        void CreateSwapchainAttachments(uint32_t width, uint32_t height,
                                        uint32_t color_sample_count);
        void DestroySwapchainAttachments();
        void DestroyAll();

    private:
        struct TargetState
        {
            VkImageLayout color_layout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkImageLayout depth_layout = VK_IMAGE_LAYOUT_UNDEFINED;
            // A compatible UNORM view avoids applying a second sRGB decode when
            // the editor samples an sRGB scene target for an UNORM presentation path.
            VkImageView editor_preview_view = VK_NULL_HANDLE;
        };

        VkDevice logical_device_ = VK_NULL_HANDLE;
        GraphicsContext context_{};
        VulkanFrameContext *frame_context_ = nullptr;
        TextureManager *texture_manager_ = nullptr;
        std::vector<RenderTargetResource> targets_;
        std::vector<TargetState> states_;
        HandleSystem<RenderTargetHandle> handles_;
        RenderTargetHandle active_target_;
        TextureHandle swapchain_depth_;
        TextureHandle swapchain_color_;
    };
}

#endif
