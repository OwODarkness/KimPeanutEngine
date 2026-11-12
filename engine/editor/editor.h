#ifndef KPENGINE_EDITOR_H
#define KPENGINE_EDITOR_H

#include <memory>
#include "editor/ui/api/editor_ui/ui_snapshot.h"
#include "editor/ui/editor_ui_session_commands.h"

namespace kpengine{

namespace runtime{
    class Engine;
}
}

namespace kpengine::editor{

    class EditorUI;

    class Editor{
    public:
        Editor();
        virtual ~Editor();
        void Initialize(kpengine::runtime::Engine* engine);
        void InitEditorUI();   // render thread — initializes loading-safe ImGui presentation
        void PromoteEditorWorkspace(); // render thread — builds scene-dependent tools
        void ActivateWorkspace(); // game thread — startup composition boundary
        void TickPresentation(); // render thread — active loading/workspace frame
        void Tick();              // compatibility forwarder to TickPresentation
        void BeginClosing();      // render thread — switches back to closing UI
        void Clear();          // main thread, after the render thread joined
        void CloseUI();        // render thread — shuts ImGui down before window teardown

    private:
        void RegisterUiSessionCommands();
        kpengine::runtime::Engine* engine_ = nullptr;
        bool initialized_ = false;
        std::shared_ptr<EditorUI> editor_ui_;
        ui::UiSnapshotStore ui_snapshot_store_;
        EditorUiSessionRegistration ui_session_registration_;
    };
}


#endif
