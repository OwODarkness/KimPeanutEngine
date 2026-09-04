#ifndef KPENGINE_EDITOR_H
#define KPENGINE_EDITOR_H

#include <memory>

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
        void TickPresentation(); // render thread — loading frame
        void Tick();           // render thread
        void Clear();          // main thread, after the render thread joined
        void CloseUI();        // render thread — shuts ImGui down before window teardown

    private:
        kpengine::runtime::Engine* engine_ = nullptr;
        bool initialized_ = false;
        std::shared_ptr<EditorUI> editor_ui_;
    };
}


#endif
