#ifndef KPENGINE_EDITOR_GLOBAL_CONTEXT_H
#define KPENGINE_EDITOR_GLOBAL_CONTEXT_H
#include<memory>
namespace kpengine{
    class WindowSystem;
    class RenderSystem;
    class LogSystem;
    class LevelSystem;
    

namespace runtime{
    class Engine;
}
namespace input{
    class InputSystem;
}

namespace editor{
    class EditorSceneManager;
    class EditorLogManager;
    class Editor;

    
    struct EditorContextInitInfo{
        WindowSystem* window_system;
        RenderSystem* render_system;
        LogSystem* log_system;
        LevelSystem* level_system;
        input::InputSystem* input_system;
        runtime::Engine* runtime_engine;
    };

    class EditorContext{
    public:
        EditorContext();
        void Initialize(const EditorContextInitInfo& init_info);
        void Clear();

        WindowSystem* window_system_{nullptr};
        RenderSystem* render_system_{nullptr};
        LogSystem* log_system_{nullptr};
        LevelSystem* level_system_{nullptr};
        input::InputSystem* input_system_{nullptr};
        runtime::Engine* runtime_engine_{nullptr}; 

        EditorSceneManager* editor_scene_manager_;
        EditorLogManager* editor_log_manager_;
        Editor* editor;
    };

    extern EditorContext global_editor_context;
}
}

#endif