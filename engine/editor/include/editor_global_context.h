#ifndef EDITOR_GLOBAL_CONTEXT_H
#define EDITOR_GLOBAL_CONTEXT_H


namespace kpengine{
    class WindowSystem;
    class SceneSystem;
namespace editor{
    class EditorSceneManager;

    struct EditorContextInitInfo{
        WindowSystem* window_system;
        SceneSystem* scene_system;
    };

    class EditorContext{
    public:
        void Initialize(const EditorContextInitInfo& init_info);

        void Clear();

        WindowSystem* window_system_{nullptr};
        SceneSystem* scene_system_{nullptr};
        
        EditorSceneManager* editor_scene_manager_;
    };

    extern EditorContext global_editor_context;
}
}

#endif