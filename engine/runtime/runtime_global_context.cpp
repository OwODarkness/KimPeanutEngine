#include "runtime_global_context.h"
#include "screenshot/runtime_screenshot_service.h"
#include "screenshot/screenshot_command_provider.h"
#include "window/window_system.h"
#include "platform/memory_stats_sampler.h"
#include "render/render_system.h"
#include "gameplay/factory/directional_light_actor_factory.h"
#include "gameplay/factory/point_light_actor_factory.h"
#include "gameplay/factory/spot_light_actor_factory.h"
#include "gameplay/factory/free_camera_actor_factory.h"
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

            // Render owns the resource pipeline and drains the async load queue the
            // bootstrap preload fed (docs/async/async_resource_queue.md).
            render::RenderSystemInitInfo render_init_info{};
            render_init_info.api_type = graphics_api_type_;
            render_init_info.native_window = window_system_->GetNativeHandle();
            render_init_info.resize_dispatcher = &window_system_->resize_event_dispatcher_;
            render_init_info.load_queue = &async_load_queue_;
            render_init_info.bootstrap_scene = bootstrap_scene_;
            render_system_->Initialize(render_init_info);
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
            // Bootstrap pass: drain the queued requests (load + process) before the
            // main loop, so first-frame pipeline requests are cache hits.
            render_system_->PostInitialize();
            bootstrap_renderable_sources_ = render_system_->TakeBootstrapRenderableSources();
        }

        void RuntimeContext::FinalizeGameStartup()
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
                return;
            }

            for (const render::StaticMeshRenderableSourceDesc &source : bootstrap_renderable_sources_)
            {
                gameplay::StaticMeshActorDesc actor_desc{};
                actor_desc.mesh_asset = source.mesh_asset;
                actor_desc.material_asset = source.material_asset;
                actor_desc.transform = source.world_transform;
                actor_desc.local_bounds = source.local_bounds;
                actor_desc.visible = source.flags.visible;
                actor_desc.casts_shadow = source.flags.casts_shadow;
                actor_desc.lod_bias = source.lod_bias;
                if (!gameplay::CreateStaticMeshActor(*gameplay_world_, actor_desc).IsValid())
                {
                    KP_LOG("RuntimeLog", LOG_LEVEL_ERROR,
                           "Bootstrap gameplay actor could not be created");
                }
            }
            bootstrap_renderable_sources_.clear();

            const gameplay::ActorHandle camera_handle =
                gameplay::CreateFreeCameraActor(*gameplay_world_);
            if (!camera_handle.IsValid())
            {
                KP_LOG("RuntimeLog", LOG_LEVEL_ERROR,
                       "Bootstrap free camera actor could not be created");
            }

            constexpr const char *kGameplayInputContext = "Gameplay";
            if (input_system_ != nullptr)
            {
                auto input_context = input_system_->GetInputContext(kGameplayInputContext);
                if (input_context == nullptr)
                {
                    input_context = std::make_shared<input::InputContext>();
                    input_system_->AddContext(kGameplayInputContext, input_context);
                }
                input_system_->SetActiveContext(kGameplayInputContext);

                gameplay::PlayerController *const controller =
                    gameplay_world_->CreateLocalPlayerController(input_system_.get(),
                                                                  kGameplayInputContext);
                if (controller == nullptr || !controller->Possess(camera_handle))
                {
                    KP_LOG("RuntimeLog", LOG_LEVEL_ERROR,
                           "Bootstrap camera controller could not possess free camera actor");
                }
            }

            // Gameplay owns authored light actors; Render observes copied source
            // snapshots through the light-source sink.
            if (!gameplay::CreateDirectionalLightActor(*gameplay_world_, {}).IsValid())
            {
                KP_LOG("RuntimeLog", LOG_LEVEL_ERROR,
                       "Bootstrap directional light actor could not be created");
            }
            const gameplay::PointLightActorDesc bootstrap_point_light{
                {0.0f, -20.0f, 65.0f}, {1.0f, 0.22f, 0.08f}, 8000.0f, 180.0f, true, true};
            if (!gameplay::CreatePointLightActor(*gameplay_world_, bootstrap_point_light).IsValid())
            {
                KP_LOG("RuntimeLog", LOG_LEVEL_ERROR,
                       "Bootstrap point light actor could not be created");
            }
            const gameplay::SpotLightActorDesc bootstrap_spot_light{
                {0.0f, 45.0f, 70.0f}, {0.0f, -0.55f, -0.85f}, {0.2f, 0.35f, 1.0f},
                24000.0f, 180.0f, 0.35f, 0.65f, true, true};
            if (!gameplay::CreateSpotLightActor(*gameplay_world_, bootstrap_spot_light).IsValid())
            {
                KP_LOG("RuntimeLog", LOG_LEVEL_ERROR,
                       "Bootstrap spot light actor could not be created");
            }

            gameplay_world_->SetLocalPlayerControllerInputEnabled(
                scene_camera_control_captured_.load(std::memory_order_acquire));
            if (input_system_ != nullptr)
            {
                input_system_->SetActiveContextEnabled(
                    scene_camera_control_captured_.load(std::memory_order_acquire));
            }
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
            gameplay_world_.reset();
            bootstrap_renderable_sources_.clear();
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

        void RuntimeContext::SetBootstrapScene(render::BootstrapSceneInfo scene)
        {
            bootstrap_scene_ = std::move(scene);
        }

        RuntimeContext::~RuntimeContext() = default;

    }
}
