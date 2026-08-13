#ifndef KPENGINE_EDITOR_UI_H
#define KPENGINE_EDITOR_UI_H

#include <vector>
#include <memory>
#include "base/type.h"

namespace kpengine
{
    class LogSystem;
}

namespace kpengine::editor
{

    class IEditorImguiRenderer;
    class IEditorImguiWSI;
    class EditorUIComponent;

    class EditorUI
    {
    public:
        EditorUI();
        ~EditorUI();

        void Initialize(WindowHandle window, GraphicsAPIType backend_type, LogSystem* log_system);
        bool Render();
        void Close();
        void BeginDraw();
        void EndDraw();

    private:
        // The UI is decoupled from any graphics API: the WSI feeds ImGui window
        // events, the renderer draws ImGui with the active backend (GL/Vulkan).
        std::unique_ptr<IEditorImguiRenderer> renderer_;
        std::unique_ptr<IEditorImguiWSI> wsi_;

        std::vector<std::unique_ptr<EditorUIComponent>> components_;
    };

}

#endif //KPENGINE_EDITOR_UI_H