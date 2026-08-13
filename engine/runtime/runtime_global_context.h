#ifndef KPENGINE_RUNTIME_GLOBAL_CONTEXT_H
#define KPENGINE_RUNTIME_GLOBAL_CONTEXT_H

#include <memory>
#include <thread>
#include "base/base.h"
#include "async/async_queue.h"
#include "asset/asset_load_request.h"

namespace kpengine::input
{
    class InputSystem;
}

namespace kpengine::render{
    class RenderSystem;
}

namespace kpengine::script::lua
{
    class LuaVM;
}

namespace kpengine
{
    constexpr float k_unit_scale = 0.01f;
    class WindowSystem;
    class AssetSystem;
    class LevelSystem;
    class LogSystem;
    class WorldSystem;


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
            std::unique_ptr<render::RenderSystem> render_system_;
            std::unique_ptr<LogSystem> log_system_;
            std::unique_ptr<input::InputSystem> input_system_;

            // Script hosting. LuaVM is engine-agnostic (script/lua/lua_vm.h); the
            // future ScriptSystem binding layer will sit above it (docs/status.md item 7).
            std::unique_ptr<script::lua::LuaVM> lua_vm_;

            std::thread::id game_thread_id_;
            std::thread::id render_thread_id_;

            GraphicsAPIType graphics_api_type_;

            // Incoming leg of the async resource queue (docs/async/async_resource_queue.md).
            // Producers (the engine's bootstrap preload today, the render module later)
            // push Queued AssetLoadRequests here; the loading thread pops them, runs
            // asset.LoadSync + resource.ProcessShader, and pushes the Ready request onto
            // the ready queue (added alongside the loading thread). RuntimeContext owns
            // it so both ends — the worker and RenderSystem's per-frame drain — can
            // reach it without a dependency cycle. Generic transport stays in core/async;
            // only the request type is asset vocabulary.
            async::AsyncQueue<asset::AssetLoadRequest> async_load_queue_;

        };

        extern RuntimeContext global_runtime_context;
    }
}

#endif