#ifndef RUNTIME_GLOBAL_CONTEXT_H
#define RUNTIME_GLOBAL_CONTEXT_H

#include <memory>

namespace kpengine
{
    class WindowSystem;
    class SceneSystem;
    namespace runtime
    {
        class RuntimeContext
        {
            public:
            RuntimeContext() = default;

            void Initialize();

            void Clear();

            std::shared_ptr<WindowSystem> window_system_;
            std::shared_ptr<SceneSystem> scene_system_;
        };

        extern RuntimeContext global_runtime_context;
    }
}

#endif
