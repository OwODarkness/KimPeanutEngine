#ifndef KPENGINE_LIVE2D_EDITOR_VIEWER_COMPONENT_H
#define KPENGINE_LIVE2D_EDITOR_VIEWER_COMPONENT_H

#include "editor/ui/component/editor_window_component.h"

namespace kpengine::live2d::editor
{
    // Temporary editor preview surface. The Cubism-backed model/render proxy
    // will replace the placeholder without changing the editor window seam.
    class Live2DEditorViewerComponent final
        : public kpengine::editor::EditorWindowComponent
    {
    public:
        Live2DEditorViewerComponent();

        void RenderContent() override;
    };
}

#endif
