#ifndef KPENGINE_RUNTIME_GLOBAL_CONTEXT_H
#define KPENGINE_RUNTIME_GLOBAL_CONTEXT_H

#include <memory>

namespace kpengine
{
    class WindowSystem;
    class RenderSystem;
    class LogSystem;

    namespace runtime
    {
        class RuntimeContext
        {
            public:
            RuntimeContext() = default;

            void Initialize();

            void Clear();

            std::shared_ptr<WindowSystem> window_system_;
            std::shared_ptr<RenderSystem> render_system_;
            std::shared_ptr<LogSystem> log_system_;
        };

        extern RuntimeContext global_runtime_context;
    }
}

#endif
