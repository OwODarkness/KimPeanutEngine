#ifndef KPENGINE_LIVE2D_EDITOR_VIEWER_COMPONENT_H
#define KPENGINE_LIVE2D_EDITOR_VIEWER_COMPONENT_H

#include "editor/ui/component/editor_window_component.h"

namespace kpengine::editor
{
    class IEditorImguiRenderer;
}

namespace kpengine::render
{
    class RenderSystem;
}

namespace kpengine::live2d::editor
{
    // Temporary editor preview surface. The Cubism-backed model/render proxy
    // will replace the placeholder without changing the editor window seam.
    class Live2DEditorViewerComponent final
        : public kpengine::editor::EditorWindowComponent
    {
    public:
        Live2DEditorViewerComponent(kpengine::render::RenderSystem *render_system,
                                    kpengine::editor::IEditorImguiRenderer *imgui_renderer);

        void RenderContent() override;

    private:
        kpengine::render::RenderSystem *render_system_ = nullptr;
        kpengine::editor::IEditorImguiRenderer *imgui_renderer_ = nullptr;
    };
}

#endif
