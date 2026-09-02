#include "runtime_global_context.h"
#include "asset/asset_manager.h"
#include "level/level_instance.h"
#include "screenshot/runtime_screenshot_service.h"
#include "screenshot/screenshot_command_provider.h"
#include "window/window_system.h"
#include "platform/memory_stats_sampler.h"
#include "render/render_system.h"
#include "gameplay/factory/directional_light_actor_factory.h"
#include "gameplay/factory/point_light_actor_factory.h"
#include "gameplay/factory/spot_light_actor_factory.h"
#include "gameplay/factory/camera_actor_factory.h"
#include "gameplay/factory/static_mesh_actor_factory.h"
#include "gameplay/controller/player_controller.h"
#include "gameplay/world/gameplay_world.h"
#include "log/log_system.h"
#include "log/logger.h"
#include "input/input_context.h"
#include "input/input_system.h"
#include "script/lua/lua_vm.h"
#include "script/command/lua_command_bridge.h"

#include <utility>
#include <stdexcept>

namespace kpengine
{
    namespace runtime
    {

        RuntimeContext global_runtime_context;

        RuntimeContext::RuntimeContext() :
        window_system_(WindowSystem::CreateWindowSystem(WindowAPIType::WINDOW_API_GLFW)),
        render_system_(std::make_unique<render::RenderSystem>()),
        command_registry_(std::make_unique<command::CommandRegistry>()),
        gameplay_world_(std::make_unique<gameplay::GameplayWorld>(
            render_system_->GetRenderableSourceSink(), render_system_->GetLightSourceSink(),
            render_system_->GetCameraSourceSink())),
        level_instance_(std::make_unique<LevelInstance>(asset::AssetManager::GetInstance(),
                                                        *gameplay_world_,
                                                        LevelActorFactorySet{},
                                                        render_system_->GetEnvironmentSourceSink())),
        log_system_(std::make_unique<LogSystem>()),
        input_system_(std::make_unique<input::InputSystem>()),
        lua_vm_(std::make_unique<::kpengine::script::lua::LuaVM>()),
        memory_sampler_(MemoryStatsSampler::CreateMemoryStatsSampler(PlatformType::PLATFORM_WINDOWS)),
        graphics_api_type_(GraphicsAPIType::GRAPHICS_API_VULKAN)
        {
            
        }

        void RuntimeContext::Initialize()
        {
            WindowCreateInfo window_create_info{};
            window_create_info.width = 1920;
            window_create_info.height = 1080;
            window_create_info.title = "KimPeanut Engine";

            window_create_info.graphics_api_type = graphics_api_type_;
            window_system_->Initialize(window_create_info);

            // Window callbacks are translated into the Runtime input boundary
            // before the Editor/ImGui layer is initialized. Editor tools may
            // then register listeners without touching GLFW callbacks directly.
            input_system_->Initialize();
            input_system_->BindMouseButtonEvent(window_system_->mouse_button_event_dispatcher_);
            input_system_->BindKeyEvent(window_system_->key_event_dispatcher_);
            input_system_->BindCursorEvent(window_system_->cursor_event_dispatcher_);
            input_system_->BindScrollEvent(window_system_->scroll_event_dispatcher_);
            input_system_->BindGamepadEvent(window_system_->gamepad_event_dispatcher_);

            lua_vm_->Initialize();

            // Render owns the resource pipeline. Startup assets were loaded by
            // Engine before this render thread was created.
            render::RenderSystemInitInfo render_init_info{};
            render_init_info.api_type = graphics_api_type_;
            render_init_info.native_window = window_system_->GetNativeHandle();
            render_init_info.resize_dispatcher = &window_system_->resize_event_dispatcher_;
            render_init_info.load_queue = &async_load_queue_;
            const render::RenderSystemInitResult render_init_result =
                render_system_->Initialize(render_init_info);
            if (!render_init_result)
            {
                throw std::runtime_error("RenderSystem initialization failed: " +
                                         render_init_result.diagnostic);
            }
            if (render::IRenderCaptureService *capture_service = render_system_->GetRenderCaptureService())
            {
                screenshot_service_ = std::make_shared<RuntimeScreenshotService>(*capture_service);
                command::CommandRegistrationResult registration =
                    RegisterScreenshotCommands(*command_registry_, screenshot_service_);
                if (!registration.IsSuccess())
                {
                    KP_LOG("RuntimeLog", LOG_LEVEL_ERROR,
                           "Could not register capture.screenshot: %s",
                           registration.diagnostic.c_str());
                }
                else
                {
                    screenshot_command_registration_ =
                        std::move(registration.registration);
                }
            }

            // [reconstruction] Old design — input/render/world were wired and initialized
            // here; reconstructed later. The bootstrap preload flow (docs/status.md item 6)
            // is the replacement entry point for the render side.
            // input_system_->BindCursorEvent(window_system_->cursor_event_dispatcher_);
            // input_system_->BindKeyEvent(window_system_->key_event_dispatcher_);
            // input_system_->BindCursorEvent(window_system_->cursor_event_dispatcher_);
            // input_system_->BindScrollEvent(window_system_->scroll_event_dispatcher_);
            // input_system_->Initialize();
            // render_system_->Initialize();
            // world_system_->Initialize();
        }

        void RuntimeContext::PostInitialize()
        {
            if (!render_system_->PostInitialize())
            {
                throw std::runtime_error(render_system_->GetLastDiagnostic().empty()
                                             ? "RenderSystem startup asset preparation failed"
                                             : render_system_->GetLastDiagnostic());
            }
        }

        RuntimeContext::StartupResult RuntimeContext::FinalizeGameStartup()
        {
            // The VM is initialized while the render thread owns startup, but
            // Engine calls this method on the game thread. Bind Lua commands here
            // so every Lua -> native command invocation shares the game lane.
            if (!lua_command_bridge_ && lua_vm_ && command_registry_)
            {
                lua_command_bridge_ = std::make_unique<::kpengine::runtime::script::LuaCommandBridge>(
                    *command_registry_, *lua_vm_);
                if (!lua_command_bridge_->Initialize())
                {
                    KP_LOG("RuntimeLog", LOG_LEVEL_ERROR,
                           "Could not initialize Lua command bridge");
                    lua_command_bridge_.reset();
                }
            }

            if (!gameplay_world_)
            {
                return {false, "GameplayWorld is unavailable"};
            }

            if (!startup_level_asset_.IsValid() ||
                startup_level_asset_.type != asset::AssetType::KPAT_Level)
            {
                return {false, "startup level AssetID is invalid"};
            }

            if (!level_instance_)
            {
                return {false, "LevelInstance is unavailable"};
            }
            const LevelInstanceResult level_result = level_instance_->Instantiate(startup_level_asset_);
            if (!level_result)
            {
                return {false, "startup level instantiation failed: " + level_result.diagnostic};
            }
            const std::optional<gameplay::ActorHandle> camera_handle =
                level_instance_->GetPreferredCameraActor();
            if (!camera_handle.has_value())
            {
                level_instance_->Unload();
                return {false, "startup level requires an enabled camera"};
            }

            constexpr const char *kGameplayInputContext = "Gameplay";
            if (input_system_ == nullptr)
            {
                level_instance_->Unload();
                return {false, "InputSystem is unavailable for startup controller"};
            }
            auto input_context = input_system_->GetInputContext(kGameplayInputContext);
            if (input_context == nullptr)
            {
                input_context = std::make_shared<input::InputContext>();
                input_system_->AddContext(kGameplayInputContext, input_context);
            }
            input_system_->SetActiveContext(kGameplayInputContext);

            bool controller_ready = false;
            if (startup_controller_setup_override_)
            {
                controller_ready = startup_controller_setup_override_(*gameplay_world_,
                                                                        input_system_.get(),
                                                                        *camera_handle);
            }
            else
            {
                gameplay::PlayerController *const controller =
                    gameplay_world_->CreateLocalPlayerController(input_system_.get(),
                                                                 kGameplayInputContext);
                controller_ready = controller != nullptr && controller->Possess(*camera_handle);
            }
            if (!controller_ready)
            {
                level_instance_->Unload();
                return {false, "startup camera controller could not possess the preferred camera"};
            }

            gameplay_world_->SetLocalPlayerControllerInputEnabled(
                scene_camera_control_captured_.load(std::memory_order_acquire));
            if (input_system_ != nullptr)
            {
                input_system_->SetActiveContextEnabled(
                    scene_camera_control_captured_.load(std::memory_order_acquire));
            }
            return {true, {}};
        }

        void RuntimeContext::TickGameplay(float delta_time)
        {
            if (!gameplay_world_)
            {
                return;
            }

            gameplay_world_->SetLocalPlayerControllerInputEnabled(
                scene_camera_control_captured_.load(std::memory_order_acquire));
            gameplay_world_->Tick(delta_time);
        }

        void RuntimeContext::SetSceneCameraControlCaptured(bool captured)
        {
            scene_camera_control_captured_.store(captured, std::memory_order_release);
            if (input_system_ != nullptr)
            {
                input_system_->SetActiveContextEnabled(captured);
            }
        }

        void RuntimeContext::Clear()
        {
            // This is called by the render thread after ImGui shuts down, while
            // the graphics context is still current. Release GPU objects before
            // the GLFW window/context they depend on.
            // Components enqueue source destruction through RenderSystem. The
            // gameplay World must therefore die before the sink and GPU teardown.
            level_instance_.reset();
            gameplay_world_.reset();
            // Drop sol2 callback closures before their Lua state and the command
            // registry they reference are torn down.
            lua_command_bridge_.reset();
            if (command_registry_)
            {
                command_registry_->Shutdown();
            }
            screenshot_command_registration_ = {};
            screenshot_service_.reset();
            if (render_system_)
            {
                render_system_->Shutdown();
                render_system_.reset();
            }
            if (input_system_)
            {
                input_system_->Shutdown();
            }
            if (window_system_)
            {
                window_system_->Cleanup();
                window_system_.reset();
            }
            log_system_.reset();
            lua_vm_.reset();
            memory_sampler_.reset();
        }

        RuntimeContext::~RuntimeContext() = default;

    }
}
