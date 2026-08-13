#ifndef KPENGINE_EDITOR_CONTEXT_H
#define KPENGINE_EDITOR_CONTEXT_H

#include<memory>
#include "base/base.h"

namespace kpengine{
    class WindowSystem;
    class LogSystem;
    class MemoryStatsSampler;

    namespace render { class RenderSystem; }


namespace runtime{
    class Engine;
}
namespace input{
    class InputSystem;
}

namespace editor{
    class Editor;


    struct EditorContextInitInfo{
        WindowSystem* window_system;
        render::RenderSystem* render_system;
        LogSystem* log_system;
        input::InputSystem* input_system;
        runtime::Engine* runtime_engine;
        MemoryStatsSampler* memory_sampler;
        GraphicsAPIType graphics_api_type;
    };

    class EditorContext{
    public:
        void Initialize(const EditorContextInitInfo& init_info);
        void Clear();
    public:
        WindowSystem* window_system_{nullptr};
        render::RenderSystem* render_system_{nullptr};
        LogSystem* log_system_{nullptr};
        input::InputSystem* input_system_{nullptr};
        runtime::Engine* runtime_engine_{nullptr};
        MemoryStatsSampler* memory_sampler_{nullptr};

        Editor* editor;

        GraphicsAPIType graphics_api_type_;
    };

    extern EditorContext global_editor_context;
}
}

#endif