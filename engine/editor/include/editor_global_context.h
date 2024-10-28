#ifndef EDITOR_GLOBAL_CONTEXT_H
#define EDITOR_GLOBAL_CONTEXT_H


namespace kpengine{
namespace editor{


    class EditorContext{


    public:
        void Initialize();

        void Clear();

        class kpengine::WindowSystem* window_system{nullptr};
    };

    extern EditorContext global_editor_context;
}
}

#endif