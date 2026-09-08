#ifndef KPENGINE_LIVE2D_EDITOR_REGISTRATION_H
#define KPENGINE_LIVE2D_EDITOR_REGISTRATION_H

namespace kpengine::editor
{
    class EditorExtensionRegistry;
}

namespace kpengine::live2d::editor
{
    // Live2D owns the editor implementation and registers it through the
    // generic editor extension seam supplied by the engine.
    void RegisterEditorExtensions(
        kpengine::editor::EditorExtensionRegistry &registry);
}

#endif
