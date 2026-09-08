#include "module/live2d/editor/live2d_editor_registration.h"

#include "editor/ui/editor_extension_registry.h"
#include "module/live2d/editor/live2d_editor_viewer_component.h"

#include <memory>

namespace kpengine::live2d::editor
{
    void RegisterEditorExtensions(
        kpengine::editor::EditorExtensionRegistry &registry)
    {
        registry.RegisterWorkspaceComponentFactory(
            []
            {
                return std::make_unique<Live2DEditorViewerComponent>();
            });
    }
}
