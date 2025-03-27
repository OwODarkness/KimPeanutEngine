#ifndef KPENGINE_RUNTIME_GLOBAL_CONTEXT_H
#define KPENGINE_RUNTIME_GLOBAL_CONTEXT_H

#include <memory>
#include <thread>
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

            std::unique_ptr<WindowSystem> window_system_;
            std::unique_ptr<RenderSystem> render_system_;
            std::unique_ptr<LogSystem> log_system_;

            std::thread::id game_thread_id;
        };

        extern RuntimeContext global_runtime_context;
    }
}

#endif