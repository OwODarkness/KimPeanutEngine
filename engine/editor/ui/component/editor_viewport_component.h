#ifndef KPENGINE_EDITOR_VIEWPORT_COMPONENT_H
#define KPENGINE_EDITOR_VIEWPORT_COMPONENT_H

#include "editor/ui/component/editor_ui_component.h"

namespace kpengine::render
{
    class RenderSystem;
}

namespace kpengine::editor
{
    class IEditorImguiRenderer;

    // Draws the render system's borrowed scene-output view. It owns neither the
    // render target nor the ImGui texture registration.
    class EditorViewportComponent final : public EditorUIComponent
    {
    public:
        EditorViewportComponent(render::RenderSystem *render_system,
                                IEditorImguiRenderer *imgui_renderer);

        void Render() override;

    private:
        render::RenderSystem *render_system_ = nullptr;
        IEditorImguiRenderer *imgui_renderer_ = nullptr;
    };
}

#endif
