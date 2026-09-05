#ifndef KPENGINE_EDITOR_IMGUI_VULKAN_RENDERER_H
#define KPENGINE_EDITOR_IMGUI_VULKAN_RENDERER_H

#include <cstdint>
#include <unordered_map>

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

        bool Initialize(graphics::IEditorPresentationBridge *presentation_bridge) override;
        void Shutdown() override;

        void NewFrame() override;
        void Render() override;
        void SetBackgroundColor(const LogColor &color) override;
        ImTextureID GetTextureID(const graphics::RenderTargetView &view) override;
        void DrawSceneImage(ImTextureID texture_id, const ImVec2 &size) override;
    private:
        void CreateDescriptorPool();
        void CreateSceneSampler();
        void ReleaseSceneTextures();

        graphics::VulkanEditorBridge *editor_bridge_ = nullptr;
        VkDevice logical_device_ = VK_NULL_HANDLE;
        VkDescriptorPool descriptor_pool_ = VK_NULL_HANDLE;
        VkSampler scene_sampler_ = VK_NULL_HANDLE;
        // Keep one descriptor per borrowed render-target view. The main
        // viewport and Debug Viewer are recorded into the same ImGui command
        // buffer and may reference different views in one frame.
        std::unordered_map<uintptr_t, VkDescriptorSet> scene_textures_;
        bool imgui_backend_initialized_ = false;
        LogColor background_color_{0.1f, 0.1f, 0.1f, 1.f};
    };

}

#endif
