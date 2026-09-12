#include "runtime_global_context.h"
#include "asset/asset_manager.h"
#include "asset/level.h"
#include "config/path.h"
#include "level/level_instance.h"
#include "screenshot/runtime_screenshot_service.h"
#include "screenshot/screenshot_command_provider.h"
#include "window/window_system.h"
#include "window/window_command_provider.h"
#include "platform/memory_stats_sampler.h"
#include "render/render_system.h"
#include "render_asset_preparer.h"
#include "gameplay/factory/directional_light_actor_factory.h"
#include "gameplay/factory/point_light_actor_factory.h"
#include "gameplay/factory/spot_light_actor_factory.h"
#include "gameplay/factory/camera_actor_factory.h"
#include "gameplay/factory/static_mesh_actor_factory.h"
#include "gameplay/controller/player_controller.h"
#include "gameplay/reflection/gameplay_reflection.h"
#include "gameplay/editor_bridge/gameplay_editor_bridge.h"
#include "gameplay/world/gameplay_world.h"
#include "reflection/entt/entt_reflection_registry.h"
#include "reflection/reflection_system.h"
#include "log/log_system.h"
#include "log/logger.h"
#include "input/input_context.h"
#include "input/input_system.h"
#include "script/lua/lua_vm.h"
#include "script/command/lua_command_bridge.h"

#include <utility>
#include <stdexcept>
#include <cmath>
#include <algorithm>

namespace kpengine
{
    namespace runtime
    {

        RuntimeContext global_runtime_context;

        RuntimeContext::RuntimeContext() :
        graphics_api_type_(GraphicsAPIType::GRAPHICS_API_VULKAN)
        {
        }

        void RuntimeContext::InitializeSceneServices()
        {
            if (render_system_ != nullptr)
            {
                return;
            }

            window_system_ = WindowSystem::CreateWindowSystem(WindowAPIType::WINDOW_API_GLFW);
            render_system_ = std::make_unique<render::RenderSystem>();
            InitializeCommandServices();
            reflection_system_ = std::make_unique<reflection::ReflectionSystem>();
            gameplay_world_ = std::make_unique<gameplay::GameplayWorld>(
                render_system_->GetRenderableSourceSink(),
                render_system_->GetLightSourceSink(),
                render_system_->GetCameraSourceSink());
            level_instance_ = std::make_unique<LevelInstance>(
                asset::AssetManager::GetInstance(), *gameplay_world_, LevelActorFactorySet{},
                render_system_->GetEnvironmentSourceSink());
            log_system_ = std::make_unique<LogSystem>();
            input_system_ = std::make_unique<input::InputSystem>();
            lua_vm_ = std::make_unique<::kpengine::script::lua::LuaVM>();
            memory_sampler_ = MemoryStatsSampler::CreateMemoryStatsSampler(
                PlatformType::PLATFORM_WINDOWS);
        }

        void RuntimeContext::EnsureSceneServices()
        {
            InitializeSceneServices();
        }

        void RuntimeContext::InitializeCommandServices()
        {
            if (command_registry_ != nullptr)
            {
                return;
            }
            command_registry_ = std::make_unique<command::CommandRegistry>();

            // Resolved per dispatch: a standalone host creates its window on the
            // render thread, so the host answer is null until that thread has
            // run, and Runtime's shared window is the Scene3D answer.
            const auto resolver = [this]() -> WindowSystem *
            {
                if (host_window_resolver_)
                {
                    if (WindowSystem *host_window = host_window_resolver_())
                    {
                        return host_window;
                    }
                }
                return window_system_.get();
            };

            command::CommandRegistrationResult registration =
                RegisterWindowCommands(*command_registry_, resolver);
            if (!registration.IsSuccess())
            {
                KP_LOG("RuntimeLog", LOG_LEVEL_ERROR,
                       "Could not register window commands: %s",
                       registration.diagnostic.c_str());
            }
            else
            {
                window_command_registration_ = std::move(registration.registration);
            }

            // The host's own providers are registered here rather than by the
            // host itself: the registry is created before the agent transport
            // starts, and a host that registered later would be writing to a
            // registry that is already answering list/execute on another thread.
            if (host_command_registrar_)
            {
                std::string host_diagnostic;
                if (!host_command_registrar_(*command_registry_, host_diagnostic))
                {
                    KP_LOG("RuntimeLog", LOG_LEVEL_ERROR,
                           "Could not register host commands: %s",
                           host_diagnostic.c_str());
                }
            }
        }

        void RuntimeContext::Initialize()
        {
            EnsureSceneServices();
            const StartupResult reflection = InitializeReflection();
            if (!reflection)
            {
                throw std::runtime_error(reflection.diagnostic);
            }
            InitializePresentation();
            const StartupResult promotion = PromoteRenderAssets();
            if (!promotion)
            {
                throw std::runtime_error(promotion.diagnostic);
            }
        }

        RuntimeContext::StartupResult RuntimeContext::InitializeReflection()
        {
            EnsureSceneServices();
            if (!reflection_system_)
            {
                reflection_system_ = std::make_unique<reflection::ReflectionSystem>();
            }
            if (reflection_system_->GetState() != reflection::ReflectionSystem::State::Frozen)
            {
                const reflection::ReflectionResult result =
                    reflection_system_->Initialize({gameplay::RegisterGameplayReflection});
                if (!result)
                {
                    return {false, "Runtime reflection initialization failed: " + result.diagnostic};
                }
            }

            if (!gameplay_world_)
            {
                return {false, "GameplayWorld is unavailable for the editor bridge"};
            }

            if (!gameplay_editor_bridge_)
            {
                const reflection::IReflectionCatalog *const catalog =
                    reflection_system_->GetCatalog();
                const reflection::IReflectionAccess *const access =
                    reflection_system_->GetAccess();
                if (catalog == nullptr || access == nullptr)
                {
                    return {false, "Reflection capabilities are unavailable for the editor bridge"};
                }

                auto bridge = std::make_unique<gameplay::GameplayEditorBridge>(
                    *gameplay_world_, *catalog, *access);
                const reflection::ReflectionResult bridge_result = bridge->Initialize();
                if (!bridge_result)
                {
                    return {false, "Gameplay editor bridge initialization failed: " +
                                       bridge_result.diagnostic};
                }
                gameplay_editor_bridge_ = std::move(bridge);
            }
            return {true, {}};
        }

        void RuntimeContext::InitializePresentation()
        {
            EnsureSceneServices();
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

            render::RenderSystemInitInfo render_init_info{};
            render_init_info.api_type = graphics_api_type_;
            render_init_info.native_window = window_system_->GetNativeHandle();
            render_init_info.resize_dispatcher = &window_system_->resize_event_dispatcher_;
            render_init_info.window_capture = [this]()
            {
                render::CaptureResult result{};
                if (!window_system_)
                {
                    result.status = render::CaptureResultStatus::Unavailable;
                    result.diagnostic = "Runtime window system is unavailable";
                    return result;
                }

                WindowCaptureResult capture = window_system_->CaptureWindow();
                if (!capture.IsSuccess())
                {
                    result.status = render::CaptureResultStatus::Unavailable;
                    result.diagnostic = std::move(capture.diagnostic);
                    return result;
                }

                result.status = render::CaptureResultStatus::Captured;
                result.image.width = capture.width;
                result.image.height = capture.height;
                result.image.rgba8_pixels = std::move(capture.rgba8_pixels);
                return result;
            };
            const render::RenderSystemInitResult render_init_result =
                render_system_->InitializePresentation(render_init_info);
            if (!render_init_result)
            {
                throw std::runtime_error("RenderSystem initialization failed: " +
                                         render_init_result.diagnostic);
            }
        }

        RuntimeContext::StartupResult RuntimeContext::PromoteRenderAssets()
        {
            EnsureSceneServices();
            if (!prepared_render_assets_)
            {
                return {false, "Render asset catalog must be prepared before scene promotion"};
            }
            const render::RenderSystemInitResult render_init_result =
                render_system_->PromoteToScene(prepared_render_assets_);
            if (!render_init_result)
            {
                return {false, "RenderSystem scene promotion failed: " +
                                   render_init_result.diagnostic};
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
            return {true, {}};
        }

        RuntimeContext::StartupResult RuntimeContext::PrepareRenderAssets()
        {
            EnsureSceneServices();
            if (!startup_level_asset_.IsValid() ||
                startup_level_asset_.type != asset::AssetType::KPAT_Level)
            {
                return {false, "startup level AssetID is invalid for render preparation"};
            }
            const RenderAssetPreparationResult result =
                RenderAssetPreparer{}.Prepare(startup_level_asset_, graphics_api_type_);
            if (!result)
            {
                return {false, result.diagnostic};
            }
            prepared_render_assets_ = result.catalog;
            if (level_instance_ != nullptr)
            {
                level_instance_->SetErrorMaterialAsset(
                    asset::AssetManager::GetInstance().LoadSync(
                        GetAssetDirectory() + asset::kEngineErrorMaterialAssetPath));
            }
            return {true, {}};
        }

        RuntimeContext::StartupResult RuntimeContext::FinalizeGameStartup()
        {
            EnsureSceneServices();
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

            if (gameplay_editor_bridge_)
            {
                gameplay_editor_bridge_->PumpEdits();
            }
            if (gameplay::PlayerController *const controller =
                    gameplay_world_->GetLocalPlayerController())
            {
                controller->SetMoveSpeed(
                    scene_camera_move_speed_.load(std::memory_order_acquire));
            }
            gameplay_world_->SetLocalPlayerControllerInputEnabled(
                scene_camera_control_captured_.load(std::memory_order_acquire));
            ProcessScenePickRequests();
            gameplay_world_->Tick(delta_time);
            if (gameplay_editor_bridge_)
            {
                gameplay_editor_bridge_->PublishSnapshot();
            }
        }

        void RuntimeContext::EnqueueScenePick(const spatial::Ray &ray)
        {
            if (!ray.IsValid())
            {
                return;
            }
            std::scoped_lock lock(scene_pick_mutex_);
            pending_scene_picks_.push_back(ray);
        }

        std::optional<ScenePickResult> RuntimeContext::ConsumeScenePickResult()
        {
            std::scoped_lock lock(scene_pick_mutex_);
            if (completed_scene_picks_.empty())
            {
                return std::nullopt;
            }
            ScenePickResult result = completed_scene_picks_.front();
            completed_scene_picks_.pop_front();
            return result;
        }

        void RuntimeContext::ProcessScenePickRequests()
        {
            std::deque<spatial::Ray> requests;
            {
                std::scoped_lock lock(scene_pick_mutex_);
                requests.swap(pending_scene_picks_);
            }

            for (const spatial::Ray &ray : requests)
            {
                const std::optional<gameplay::ActorHandle> actor =
                    gameplay_world_ ? gameplay_world_->PickActor(ray) : std::nullopt;
                if (gameplay_world_)
                {
                    gameplay_world_->SetSelectedActor(actor);
                }
                ScenePickResult result{};
                if (actor.has_value())
                {
                    result.hit = true;
                    result.actor = *actor;
                    KP_LOG("LogEditorSelection", LOG_LEVEL_DEBUG,
                           "Selected object: Actor %u:%u", actor->id,
                           static_cast<unsigned int>(actor->generation));
                }
                else
                {
                    KP_LOG("LogEditorSelection", LOG_LEVEL_DEBUG,
                           "Selected object: <none>");
                }

                std::scoped_lock lock(scene_pick_mutex_);
                completed_scene_picks_.push_back(result);
            }
        }

        void RuntimeContext::SetSceneCameraControlCaptured(bool captured)
        {
            scene_camera_control_captured_.store(captured, std::memory_order_release);
            if (input_system_ != nullptr)
            {
                input_system_->SetActiveContextEnabled(captured);
            }
        }

        float RuntimeContext::GetSceneCameraMoveSpeed() const noexcept
        {
            return scene_camera_move_speed_.load(std::memory_order_acquire);
        }

        void RuntimeContext::SetSceneCameraMoveSpeed(float units_per_second)
        {
            if (std::isfinite(units_per_second))
            {
                scene_camera_move_speed_.store(
                    std::clamp(units_per_second, kMinimumSceneCameraMoveSpeed,
                               kMaximumSceneCameraMoveSpeed),
                    std::memory_order_release);
            }
        }

        void RuntimeContext::Clear(ShutdownProgressCallback progress_callback)
        {
            constexpr uint32_t shutdown_units = 7;
            const auto report_progress = [&progress_callback, shutdown_units](
                uint32_t completed_units, const char *label)
            {
                if (!progress_callback)
                {
                    return;
                }
                progress_callback(ShutdownProgress{
                    completed_units, shutdown_units, label != nullptr ? label : ""});
            };

            report_progress(0, "Stopping gameplay");
            scene_camera_control_captured_.store(false, std::memory_order_release);
            scene_camera_move_speed_.store(kDefaultSceneCameraMoveSpeed,
                                           std::memory_order_release);
            {
                std::scoped_lock lock(scene_pick_mutex_);
                pending_scene_picks_.clear();
                completed_scene_picks_.clear();
            }
            // This is called by the render thread while ImGui and the graphics
            // context are still alive. Release GPU objects before the GLFW
            // window/context they depend on.
            // Components enqueue source destruction through RenderSystem. The
            // gameplay World must therefore die before the sink and GPU teardown.
            if (gameplay_editor_bridge_)
            {
                gameplay_editor_bridge_->Shutdown();
                gameplay_editor_bridge_.reset();
            }
            report_progress(1, "Releasing level");
            level_instance_.reset();
            gameplay_world_.reset();
            report_progress(2, "Stopping reflection and scripting");
            if (reflection_system_)
            {
                reflection_system_->Shutdown();
                reflection_system_.reset();
            }
            // Drop sol2 callback closures before their Lua state and the command
            // registry they reference are torn down.
            lua_command_bridge_.reset();
            if (command_registry_)
            {
                command_registry_->Shutdown();
            }
            screenshot_command_registration_ = {};
            screenshot_service_.reset();
            report_progress(3, "Releasing renderer");
            if (render_system_)
            {
                render_system_->Shutdown();
                render_system_.reset();
            }
            report_progress(4, "Releasing prepared assets");
            prepared_render_assets_.reset();
            report_progress(5, "Releasing input");
            if (input_system_)
            {
                input_system_->Shutdown();
            }
            report_progress(6, "Closing window");
            if (window_system_)
            {
                window_system_->Cleanup();
                window_system_.reset();
            }
            report_progress(7, "Shutdown complete");
            log_system_.reset();
            lua_vm_.reset();
            memory_sampler_.reset();
        }

        RuntimeContext::~RuntimeContext() = default;

        const reflection::IReflectionCatalog *RuntimeContext::GetReflectionCatalog() const noexcept
        {
            return reflection_system_ != nullptr ? reflection_system_->GetCatalog() : nullptr;
        }

        gameplay::IGameplayEditorSnapshotSource *RuntimeContext::GetGameplayEditorSnapshotSource() noexcept
        {
            return gameplay_editor_bridge_.get();
        }

        gameplay::IGameplayEditorEditSink *RuntimeContext::GetGameplayEditorEditSink() noexcept
        {
            return gameplay_editor_bridge_.get();
        }

    }
}
