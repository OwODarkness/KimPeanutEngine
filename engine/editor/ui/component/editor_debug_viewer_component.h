#ifndef KPENGINE_EDITOR_DEBUG_VIEWER_COMPONENT_H
#define KPENGINE_EDITOR_DEBUG_VIEWER_COMPONENT_H

#include "editor/ui/component/editor_window_component.h"
#include "render/render_capture_service.h"

namespace kpengine::render
{
    class RenderSystem;
}

namespace kpengine::editor
{
    class IEditorImguiRenderer;

    // Independent, lockable ImGui window for comparing Render-owned diagnostic
    // output against the main Scene Color viewport.
    class EditorDebugViewerComponent final : public EditorWindowComponent
    {
    public:
        EditorDebugViewerComponent(render::RenderSystem *render_system,
                                   IEditorImguiRenderer *imgui_renderer);

        void RenderContent() override;

    private:
        render::RenderSystem *render_system_ = nullptr;
        IEditorImguiRenderer *imgui_renderer_ = nullptr;
        render::CaptureView debug_view_ = render::CaptureView::WorldNormal;
    };
}

#endif
