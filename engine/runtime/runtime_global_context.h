#ifndef KPENGINE_RUNTIME_GLOBAL_CONTEXT_H
#define KPENGINE_RUNTIME_GLOBAL_CONTEXT_H

#include <memory>
#include <thread>
#include "runtime/graphics/backend/common/api.h"

namespace kpengine::input
{
    class InputSystem;
}

namespace kpengine
{
    constexpr float k_unit_scale = 0.01f;
    class WindowSystem;
    class RenderSystem;
    class AssetSystem;
    class LevelSystem;
    class LogSystem;
    class WorldSystem;

    struct GLFWAppContext
    {
        WindowSystem *window_sys;
        input::InputSystem *input_sys;
    };

    namespace runtime
    {

        class RuntimeContext
        {

        public:
            RuntimeContext();
            ~RuntimeContext();
            void Initialize();
            void PostInitialize();
            void Clear();

        public:
            std::unique_ptr<WindowSystem> window_system_;
            std::unique_ptr<RenderSystem> render_system_;
            std::unique_ptr<LogSystem> log_system_;
            std::unique_ptr<AssetSystem> asset_system_;
            std::unique_ptr<WorldSystem> world_system_;
            std::unique_ptr<input::InputSystem> input_system_;

            std::thread::id game_thread_id_;
            std::thread::id render_thread_id_;

            GraphicsBackEndType graphics_backend_type_;

        private:
            std::shared_ptr<GLFWAppContext> glfw_context_;
        };

        extern RuntimeContext global_runtime_context;
    }
}

#endif