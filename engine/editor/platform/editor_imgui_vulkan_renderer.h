#ifndef KPENGINE_EDITOR_IMGUI_VULKAN_RENDERER_H
#define KPENGINE_EDITOR_IMGUI_VULKAN_RENDERER_H

#include <vulkan/vulkan.h>

#include "editor/platform/editor_imgui_renderer.h"

namespace kpengine::graphics{
    class VulkanEditorBridge;
}

namespace kpengine::editor
{
    class EditorImguiVulkanRenderer : public IEditorImguiRenderer
    {
    public:
        ~EditorImguiVulkanRenderer() = default;

        void Initialize(GraphicsContext context) override;
        void Shutdown() override;

        void NewFrame() override;
        void Render() override;
        void SetBackgroundColor(const LogColor &color) override;
        ImTextureID GetTextureID(const graphics::RenderTargetView &view) override;
        void DrawSceneImage(ImTextureID texture_id, const ImVec2 &size) override;
    private:
        void CreateDescriptorPool();
        void CreateSceneSampler();
        void ReleaseSceneTexture();

        graphics::VulkanEditorBridge *editor_bridge_ = nullptr;
        VkDevice logical_device_ = VK_NULL_HANDLE;
        VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
        VkSampler scene_sampler_ = VK_NULL_HANDLE;
        VkImageView scene_view_ = VK_NULL_HANDLE;
        VkDescriptorSet scene_texture_ = VK_NULL_HANDLE;
        LogColor background_color_{0.1f, 0.1f, 0.1f, 1.f};
    };

}

#endif
