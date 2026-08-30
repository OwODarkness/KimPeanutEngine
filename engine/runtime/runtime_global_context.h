#ifndef KPENGINE_RUNTIME_GLOBAL_CONTEXT_H
#define KPENGINE_RUNTIME_GLOBAL_CONTEXT_H

#include <atomic>
#include <memory>
#include <optional>
#include <thread>
#include <vector>
#include "base/base.h"
#include "async/async_queue.h"
#include "asset/asset_load_request.h"
#include "command/command_registry.h"
#include "render/render_system.h"
#include "runtime_camera_control.h"

namespace kpengine::gameplay
{
    class GameplayWorld;
}

namespace kpengine::input
{
    class InputSystem;
}

namespace kpengine::script::lua
{
    class LuaVM;
}

namespace kpengine::runtime::script
{
    class LuaCommandBridge;
}

namespace kpengine
{
    constexpr float k_unit_scale = 0.01f;
    class WindowSystem;
    class AssetSystem;
    class LevelSystem;
    class LogSystem;
    class WorldSystem;
    class MemoryStatsSampler;


    namespace runtime
    {
        class RuntimeScreenshotService;

        class RuntimeContext final : public ISceneCameraControlSink
        {

        public:
            RuntimeContext();
            ~RuntimeContext();
            void Initialize();
            void PostInitialize();
            // Called by Engine after the render startup handshake, on the game thread.
            // This is the Runtime-owned boundary for initial World composition.
            void FinalizeGameStartup();
            void Clear();
            void TickGameplay(float delta_time);
            void SetBootstrapScene(render::BootstrapSceneInfo scene);
            void SetSceneCameraControlCaptured(bool captured) override;
            render::IRenderCaptureService *GetRenderCaptureService()
            {
                return render_system_ ? render_system_->GetRenderCaptureService() : nullptr;
            }
            RuntimeScreenshotService *GetScreenshotService() { return screenshot_service_.get(); }
            command::CommandRegistry *GetCommandRegistry() { return command_registry_.get(); }

        public:
            std::unique_ptr<WindowSystem> window_system_;
            std::unique_ptr<render::RenderSystem> render_system_;
            std::unique_ptr<command::CommandRegistry> command_registry_;
            std::shared_ptr<RuntimeScreenshotService> screenshot_service_;
            command::CommandRegistration screenshot_command_registration_;
            std::unique_ptr<gameplay::GameplayWorld> gameplay_world_;
            std::unique_ptr<LogSystem> log_system_;
            std::unique_ptr<input::InputSystem> input_system_;

            // Platform seam for OS-level measurement (memory stats). Chosen by PlatformType
            // like the window system; the editor reads it through EditorContext, never the OS.
            std::unique_ptr<MemoryStatsSampler> memory_sampler_;

            // Script hosting. LuaVM is engine-agnostic (script/lua/lua_vm.h); the
            // future ScriptSystem binding layer will sit above it (docs/status.md item 7).
            std::unique_ptr<::kpengine::script::lua::LuaVM> lua_vm_;
            std::unique_ptr<::kpengine::runtime::script::LuaCommandBridge> lua_command_bridge_;

            std::thread::id game_thread_id_;
            std::thread::id render_thread_id_;

            GraphicsAPIType graphics_api_type_;
            render::BootstrapSceneInfo bootstrap_scene_;
            std::vector<render::StaticMeshRenderableSourceDesc> bootstrap_renderable_sources_;

            // Incoming leg of the async resource queue (docs/async/async_resource_queue.md).
            // Producers (the engine's bootstrap preload today, the render module later)
            // push Queued AssetLoadRequests here; the loading thread pops them, runs
            // asset.LoadSync + resource.ProcessShader, and pushes the Ready request onto
            // the ready queue (added alongside the loading thread). RuntimeContext owns
            // it so both ends — the worker and RenderSystem's per-frame drain — can
            // reach it without a dependency cycle. Generic transport stays in core/async;
            // only the request type is asset vocabulary.
            async::AsyncQueue<asset::AssetLoadRequest> async_load_queue_;

        private:
            std::atomic<bool> scene_camera_control_captured_{false};

        };

        extern RuntimeContext global_runtime_context;
    }
}

#endif
