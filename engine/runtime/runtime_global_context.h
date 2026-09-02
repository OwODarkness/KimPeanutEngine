#ifndef KPENGINE_RUNTIME_GLOBAL_CONTEXT_H
#define KPENGINE_RUNTIME_GLOBAL_CONTEXT_H

#include <atomic>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "base/base.h"
#include "command/command_registry.h"
#include "render/render_system.h"
#include "render_asset_preparer.h"
#include "runtime_camera_control.h"
#include "gameplay/actor/actor_types.h"

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
        class LevelInstance;

        class RuntimeContext final : public ISceneCameraControlSink
        {

        public:
            RuntimeContext();
            ~RuntimeContext();
            void Initialize();
            void PostInitialize();
            // Called by Engine after the render startup handshake, on the game thread.
            // This is the Runtime-owned boundary for initial World composition.
            struct StartupResult
            {
                bool success = false;
                std::string diagnostic;
                explicit operator bool() const { return success; }
            };
            StartupResult FinalizeGameStartup();
            // Runtime-only startup transaction: freeze the CPU render catalog
            // before the render thread creates any GPU state.
            StartupResult PrepareRenderAssets();
            // Narrow test seam for the game-start controller/possession step.
            // Production leaves this unset and uses GameplayWorld's real
            // controller implementation.
            using StartupControllerSetupOverride =
                std::function<bool(gameplay::GameplayWorld &, input::InputSystem *,
                                   gameplay::ActorHandle)>;
            void SetStartupControllerSetupOverride(StartupControllerSetupOverride override)
            {
                startup_controller_setup_override_ = std::move(override);
            }
            void Clear();
            void TickGameplay(float delta_time);
            void SetStartupLevel(asset::AssetID level_asset) { startup_level_asset_ = level_asset; }
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
            // Owns the committed startup level. Its destructor must unload
            // level-created Actors before GameplayWorld and RenderSystem.
            std::unique_ptr<LevelInstance> level_instance_;
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
            asset::AssetID startup_level_asset_;

            std::shared_ptr<const render::PreparedRenderAssetCatalog> prepared_render_assets_;

        private:
            std::atomic<bool> scene_camera_control_captured_{false};
            StartupControllerSetupOverride startup_controller_setup_override_;

        };

        extern RuntimeContext global_runtime_context;
    }
}

#endif
