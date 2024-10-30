#ifndef EDITOR_GLOBAL_CONTEXT_H
#define EDITOR_GLOBAL_CONTEXT_H


namespace kpengine{
    class WindowSystem;


namespace editor{

    struct EditorContextInitInfo{
        WindowSystem* window_system;
    };

    class EditorContext{
    public:
        void Initialize(const EditorContextInitInfo& init_info);

        void Clear();

        WindowSystem* window_system_{nullptr};
    };

    extern EditorContext global_editor_context;
}
}

#endif