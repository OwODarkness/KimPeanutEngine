#ifndef KPENGINE_EDITOR_IMGUI_VULKAN_RENDERER_H
#define KPENGINE_EDITOR_IMGUI_VULKAN_RENDERER_H

#include "editor_imgui_renderer.h"

namespace kpengine::graphics{
    struct VulkanContext;
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
    private:
        graphics::VulkanContext* vulkan_ctx;
    };

}

#endif