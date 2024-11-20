#ifndef KPENGINE_EDITOR_GLOBAL_CONTEXT_H
#define KPENGINE_EDITOR_GLOBAL_CONTEXT_H


namespace kpengine{
    class WindowSystem;
    class RenderSystem;

namespace runtime{
    class Engine;
}

namespace editor{
    class EditorSceneManager;
    class EditorInputManager;

    struct EditorContextInitInfo{
        WindowSystem* window_system;
        RenderSystem* render_system;
        runtime::Engine* runtime_engine;
    };

    class EditorContext{
    public:
        void Initialize(const EditorContextInitInfo& init_info);

        void Clear();

        WindowSystem* window_system_{nullptr};
        RenderSystem* render_system_{nullptr};
        runtime::Engine* runtime_engine_{nullptr}; 

        EditorSceneManager* editor_scene_manager_;
        EditorInputManager* editor_input_manager_;
    };

    extern EditorContext global_editor_context;
}
}

#endif