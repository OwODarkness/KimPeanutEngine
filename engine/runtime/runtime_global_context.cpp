#include "runtime_global_context.h"
#include <limits>
#include <cstdint>
#include "asset/asset_manager.h"
#include "asset/asset_catalog_snapshot_provider.h"
#include "asset/level.h"
#include "config/path.h"
#include "level/level_instance.h"
#include "screenshot/runtime_screenshot_service.h"
#include "screenshot/screenshot_command_provider.h"
#include "window/window_system.h"
#include "window/window_command_provider.h"
#include "gameplay_command/gameplay_command_provider.h"
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
            if (command_registry_ != nullptr)
            {
                GameplayCommandRegistrationResult gameplay_commands =
                    RegisterGameplayCommands(*command_registry_, [this]()
                    {
                        return gameplay_world_.get();
                    });
                if (!gameplay_commands.IsSuccess())
                {
                    throw std::runtime_error(
                        "Could not register Gameplay commands: " +
                        gameplay_commands.diagnostic);
                }
                gameplay_command_registrations_ =
                    std::move(gameplay_commands.registrations);
            }
            level_instance_ = std::make_unique<LevelInstance>(
                asset::AssetManager::GetInstance(), *gameplay_world_, LevelActorFactorySet{},
                render_system_->GetEnvironmentSourceSink());
            if (command_registry_ != nullptr)
            {
                command::CommandRegistrationResult registration = command_registry_->Register(
                    {"level.reload",
                     "RuntimeLevel",
                     "Unload and recreate the active startup level",
                     command::CommandCategory::Gameplay,
                     command::CommandFlags::AgentAllowed | command::CommandFlags::MutatesState,
                     {},
                     [this](const command::CommandCall &, const command::CommandContext &context)
                     {
                         const StartupResult result = ReloadStartupLevel();
                         return command::CommandResult{
                             result.success ? command::CommandStatus::Success
                                            : command::CommandStatus::Failed,
                             result.success ? "Startup level reloaded" : result.diagnostic,
                             context.request_id,
                             {}};
                     },
                     command::CommandThread::Game});
                if (registration.IsSuccess())
                {
                    level_reload_command_registration_ =
                        std::move(registration.registration);
                }
                else
                {
                    KP_LOG("RuntimeLog", LOG_LEVEL_ERROR,
                           "Could not register level.reload: %s",
                           registration.diagnostic.c_str());
                }

                command::CommandRegistrationResult probe_registration =
                    command_registry_->Register(
                        {"render.path_trace_probe",
                         "RenderDiagnostics",
                         "Select the RT path-tracing diagnostic output",
                         command::CommandCategory::Render,
                         command::CommandFlags::AgentAllowed |
                             command::CommandFlags::MutatesState,
                         {{command::CommandArgumentDesc{
                             "mode",
                             command::CommandValueType::Enum,
                             true,
                              {},
                             {"beauty", "primary_visibility", "primary_normal",
                               "primary_albedo", "direct_only", "surface_parameters",
                               "ray_query_visibility", "ray_cone_filtering",
                               "low_spp_preview", "beauty_denoise",
                               "single_light_preview"}}}},
                         [this](const command::CommandCall &call,
                                const command::CommandContext &context)
                         {
                             const std::string &mode =
                                 std::get<std::string>(call.arguments.at("mode"));
                             render::PathTraceProbeMode probe_mode =
                                 render::PathTraceProbeMode::Beauty;
                             if (mode == "primary_visibility")
                                 probe_mode = render::PathTraceProbeMode::PrimaryVisibility;
                             else if (mode == "primary_normal")
                                 probe_mode = render::PathTraceProbeMode::PrimaryNormal;
                             else if (mode == "primary_albedo")
                                 probe_mode = render::PathTraceProbeMode::PrimaryAlbedo;
                             else if (mode == "direct_only")
                                 probe_mode = render::PathTraceProbeMode::DirectOnly;
                             else if (mode == "surface_parameters")
                                 probe_mode = render::PathTraceProbeMode::SurfaceParameters;
                             else if (mode == "ray_query_visibility")
                                 probe_mode = render::PathTraceProbeMode::RayQueryVisibility;
                             else if (mode == "ray_cone_filtering")
                                 probe_mode = render::PathTraceProbeMode::RayConeFiltering;
                             else if (mode == "low_spp_preview")
                                 probe_mode = render::PathTraceProbeMode::LowSppPreview;
                             else if (mode == "beauty_denoise")
                                 probe_mode = render::PathTraceProbeMode::BeautyDenoise;
                             else if (mode == "single_light_preview")
                                 probe_mode = render::PathTraceProbeMode::SingleLightPreview;
                             if (!render_system_)
                             {
                                 return command::CommandResult{
                                     command::CommandStatus::Failed,
                                     "RenderSystem is unavailable",
                                     context.request_id,
                                     {}};
                             }
                             render_system_->RequestPathTraceProbeMode(probe_mode);
                             return command::CommandResult{
                                 command::CommandStatus::Success,
                                 "Path-trace probe mode scheduled: " + mode,
                                 context.request_id,
                                 {{"mode", mode}}};
                         },
                         command::CommandThread::Game});
                if (probe_registration.IsSuccess())
                {
                    path_trace_probe_command_registration_ =
                        std::move(probe_registration.registration);
                }
                else
                {
                    KP_LOG("RuntimeLog", LOG_LEVEL_ERROR,
                           "Could not register render.path_trace_probe: %s",
                           probe_registration.diagnostic.c_str());
                }

                command::CommandRegistrationResult settings_registration =
                    command_registry_->Register(
                        {"render.path_trace_settings",
                         "RenderSettings",
                         "Set independent path-tracing work, visibility, reconstruction and lighting "
                         "settings",
                         command::CommandCategory::Render,
                         command::CommandFlags::AgentAllowed |
                             command::CommandFlags::MutatesState,
                         {{command::CommandArgumentDesc{
                               "enabled", command::CommandValueType::Boolean, true, {}, {}},
                           command::CommandArgumentDesc{
                               "hybrid_ray_query_shadows", command::CommandValueType::Boolean,
                               true, {}, {}},
                           command::CommandArgumentDesc{
                               "visibility_method", command::CommandValueType::Enum, true, {},
                               {"ray_pipeline", "ray_query"}},
                           command::CommandArgumentDesc{
                               "samples_per_dispatch", command::CommandValueType::UnsignedInteger,
                               true, {}, {}},
                           command::CommandArgumentDesc{
                               "maximum_continuation_bounces",
                               command::CommandValueType::UnsignedInteger, true, {}, {}},
                           command::CommandArgumentDesc{
                               "reconstruction", command::CommandValueType::Enum, true, {},
                               {"raw", "guided_preview", "variance_denoise"}},
                           command::CommandArgumentDesc{
                               "adaptive_moving_reconstruction",
                               command::CommandValueType::Enum, true, {},
                               {"raw", "guided_preview", "variance_denoise"}},
                           command::CommandArgumentDesc{
                               "direct_light_sampling", command::CommandValueType::Enum, true,
                               {}, {"all_lights", "uniform_one_light"}},
                           command::CommandArgumentDesc{
                               "sampling_policy", command::CommandValueType::Enum, true,
                               {}, {"fixed", "adaptive_camera_motion"}},
                           command::CommandArgumentDesc{
                               "moving_samples_per_dispatch",
                               command::CommandValueType::UnsignedInteger, true, {}, {}},
                           command::CommandArgumentDesc{
                               "settled_samples_per_dispatch",
                               command::CommandValueType::UnsignedInteger, true, {}, {}},
                           command::CommandArgumentDesc{
                               "quality_2spp_samples_per_dispatch",
                               command::CommandValueType::UnsignedInteger, true, {}, {}},
                           command::CommandArgumentDesc{
                               "quality_2spp_sample_threshold",
                               command::CommandValueType::UnsignedInteger, true, {}, {}},
                           command::CommandArgumentDesc{
                               "quality_1spp_sample_threshold",
                               command::CommandValueType::UnsignedInteger, true, {}, {}},
                           command::CommandArgumentDesc{
                               "quality_maintenance_samples_per_dispatch",
                               command::CommandValueType::UnsignedInteger, true, {}, {}},
                           command::CommandArgumentDesc{
                               "settle_frame_threshold",
                               command::CommandValueType::UnsignedInteger, true, {}, {}},
                           command::CommandArgumentDesc{
                               "camera_translation_threshold",
                               command::CommandValueType::Float, true, {}, {}},
                           command::CommandArgumentDesc{
                               "camera_rotation_threshold_degrees",
                               command::CommandValueType::Float, true, {}, {}},
                           command::CommandArgumentDesc{
                               "output_probe", command::CommandValueType::Enum, true, {},
                               {"beauty", "primary_visibility", "primary_normal",
                                "primary_albedo", "direct_only", "surface_parameters",
                                "ray_cone_filtering"}}}},
                         [this](const command::CommandCall &call,
                                const command::CommandContext &context)
                         {
                             if (!render_system_)
                             {
                                 return command::CommandResult{
                                     command::CommandStatus::Failed,
                                     "RenderSystem is unavailable",
                                     context.request_id,
                                     {}};
                             }

                             const auto visibility = render::ParsePathTraceVisibilityMethod(
                                 std::get<std::string>(call.arguments.at("visibility_method")));
                             const auto reconstruction = render::ParsePathTraceReconstruction(
                                 std::get<std::string>(call.arguments.at("reconstruction")));
                             const auto adaptive_moving_reconstruction =
                                 render::ParsePathTraceReconstruction(std::get<std::string>(
                                     call.arguments.at("adaptive_moving_reconstruction")));
                             const auto light_sampling =
                                 render::ParsePathTraceDirectLightSampling(
                                     std::get<std::string>(
                                         call.arguments.at("direct_light_sampling")));
                             const auto sampling_policy = render::ParsePathTraceSamplingPolicy(
                                 std::get<std::string>(call.arguments.at("sampling_policy")));
                             const auto output_probe = render::ParsePathTraceOutputProbe(
                                 std::get<std::string>(call.arguments.at("output_probe")));
                             const uint64_t samples = std::get<uint64_t>(
                                 call.arguments.at("samples_per_dispatch"));
                             const uint64_t bounces = std::get<uint64_t>(
                                 call.arguments.at("maximum_continuation_bounces"));
                             const uint64_t moving_samples = std::get<uint64_t>(
                                 call.arguments.at("moving_samples_per_dispatch"));
                             const uint64_t settled_samples = std::get<uint64_t>(
                                 call.arguments.at("settled_samples_per_dispatch"));
                             const uint64_t maintenance_samples = std::get<uint64_t>(
                                 call.arguments.at("quality_maintenance_samples_per_dispatch"));
                             const uint64_t quality_2spp_samples = std::get<uint64_t>(
                                 call.arguments.at("quality_2spp_samples_per_dispatch"));
                             const uint64_t quality_2spp_threshold = std::get<uint64_t>(
                                 call.arguments.at("quality_2spp_sample_threshold"));
                             const uint64_t quality_1spp_threshold = std::get<uint64_t>(
                                 call.arguments.at("quality_1spp_sample_threshold"));
                             const uint64_t settle_frames = std::get<uint64_t>(
                                 call.arguments.at("settle_frame_threshold"));
                             if (!visibility || !reconstruction ||
                                 !adaptive_moving_reconstruction || !light_sampling ||
                                 !sampling_policy || !output_probe ||
                                 samples > std::numeric_limits<uint32_t>::max() ||
                                 bounces > std::numeric_limits<uint32_t>::max() ||
                                 moving_samples > std::numeric_limits<uint32_t>::max() ||
                                 settled_samples > std::numeric_limits<uint32_t>::max() ||
                                 maintenance_samples > std::numeric_limits<uint32_t>::max() ||
                                 quality_2spp_samples > std::numeric_limits<uint32_t>::max() ||
                                 quality_2spp_threshold > std::numeric_limits<uint32_t>::max() ||
                                 quality_1spp_threshold > std::numeric_limits<uint32_t>::max() ||
                                 settle_frames > std::numeric_limits<uint32_t>::max())
                             {
                                 return command::CommandResult{
                                     command::CommandStatus::InvalidArguments,
                                     "Path-tracing settings are invalid",
                                     context.request_id,
                                     {}};
                             }

                             render::PathTraceSettings settings{};
                             settings.path_tracing_enabled = std::get<bool>(
                                 call.arguments.at("enabled"));
                             settings.hybrid_ray_query_shadows_enabled = std::get<bool>(
                                 call.arguments.at("hybrid_ray_query_shadows"));
                             settings.visibility_method = *visibility;
                             settings.samples_per_dispatch = static_cast<uint32_t>(samples);
                             settings.maximum_continuation_bounces =
                                 static_cast<uint32_t>(bounces);
                             settings.reconstruction = *reconstruction;
                             settings.adaptive_moving_reconstruction =
                                 *adaptive_moving_reconstruction;
                             settings.direct_light_sampling = *light_sampling;
                             settings.sampling_policy = *sampling_policy;
                             settings.moving_samples_per_dispatch =
                                 static_cast<uint32_t>(moving_samples);
                             settings.settled_samples_per_dispatch =
                                 static_cast<uint32_t>(settled_samples);
                             settings.quality_maintenance_samples_per_dispatch =
                                 static_cast<uint32_t>(maintenance_samples);
                             settings.quality_2spp_samples_per_dispatch =
                                 static_cast<uint32_t>(quality_2spp_samples);
                             settings.quality_2spp_sample_threshold =
                                 static_cast<uint32_t>(quality_2spp_threshold);
                             settings.quality_1spp_sample_threshold =
                                 static_cast<uint32_t>(quality_1spp_threshold);
                             settings.settle_frame_threshold =
                                 static_cast<uint32_t>(settle_frames);
                             settings.camera_translation_threshold = static_cast<float>(
                                 std::get<double>(
                                     call.arguments.at("camera_translation_threshold")));
                             settings.camera_rotation_threshold_degrees = static_cast<float>(
                                 std::get<double>(call.arguments.at(
                                     "camera_rotation_threshold_degrees")));
                             settings.output_probe = *output_probe;
                             if (!render_system_->RequestPathTraceSettings(settings))
                             {
                                 return command::CommandResult{
                                     command::CommandStatus::InvalidArguments,
                                     "Path-tracing settings are outside the supported range",
                                     context.request_id,
                                     {}};
                             }
                             return command::CommandResult{
                                 command::CommandStatus::Success,
                                 "Path-tracing settings scheduled",
                                 context.request_id,
                                 {}};
                         },
                         command::CommandThread::Game});
                if (settings_registration.IsSuccess())
                {
                    path_trace_settings_command_registration_ =
                        std::move(settings_registration.registration);
                }
                else
                {
                    KP_LOG("RuntimeLog", LOG_LEVEL_ERROR,
                           "Could not register render.path_trace_settings: %s",
                           settings_registration.diagnostic.c_str());
                }

                command::CommandRegistrationResult failure_registration =
                    command_registry_->Register(
                        {"render.path_trace_fail_next",
                         "RenderDiagnostics",
                         "Reject one path-trace pass before backend dispatch for recovery validation",
                         command::CommandCategory::Render,
                         command::CommandFlags::AgentAllowed |
                             command::CommandFlags::MutatesState,
                         {},
                         [this](const command::CommandCall &,
                                const command::CommandContext &context)
                         {
                             if (!render_system_)
                             {
                                 return command::CommandResult{
                                     command::CommandStatus::Failed,
                                     "RenderSystem is unavailable",
                                     context.request_id,
                                     {}};
                             }
                             render_system_->RequestPathTraceDispatchFailureInjection();
                             return command::CommandResult{
                                 command::CommandStatus::Success,
                                 "One path-trace pass rejection scheduled",
                                 context.request_id,
                                 {}};
                         },
                         command::CommandThread::Game});
                if (failure_registration.IsSuccess())
                {
                    path_trace_failure_command_registration_ =
                        std::move(failure_registration.registration);
                }
                else
                {
                    KP_LOG("RuntimeLog", LOG_LEVEL_ERROR,
                           "Could not register render.path_trace_fail_next: %s",
                           failure_registration.diagnostic.c_str());
                }
            }
            // The Asset-owned catalog boundary the Editor's browser reads through. Its
            // config is left empty on purpose: the provider already resolves an empty
            // database path to <asset>/.archive/archive.sqlite3 and derives the archive
            // root from it, so naming a path here could only get that wrong. Constructing
            // it opens nothing — the first CaptureAssetCatalog() is what reads.
            asset_catalog_provider_ = std::make_unique<asset::AssetCatalogSnapshotProvider>(
                asset::AssetManager::GetInstance(), asset::AssetCatalogProviderConfig{});
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
            window_create_info.vsync = vsync_;
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
            render_init_info.ray_tracing_enabled = ray_tracing_enabled_;
            render_init_info.path_tracing_enabled = path_tracing_enabled_;
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
            if (render_system_->GetRenderCaptureService() != nullptr)
            {
                screenshot_service_ = std::make_shared<RuntimeScreenshotService>([this] {
                    return render_system_ ? render_system_->GetRenderCaptureService() : nullptr;
                });
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

        RuntimeContext::StartupResult RuntimeContext::ReloadStartupLevel()
        {
            if (gameplay_world_ == nullptr || level_instance_ == nullptr ||
                input_system_ == nullptr)
            {
                return {false, "Scene services are unavailable for level reload"};
            }
            if (!startup_level_asset_.IsValid() ||
                startup_level_asset_.type != asset::AssetType::KPAT_Level)
            {
                return {false, "Startup level AssetID is invalid"};
            }
            if (!level_instance_->IsActive())
            {
                return {false, "There is no active startup level to reload"};
            }
            if (!startup_controller_setup_override_ &&
                gameplay_world_->GetLocalPlayerController() == nullptr)
            {
                return {false, "Startup level reload requires a local player controller"};
            }

            gameplay_world_->SetSelectedActor(std::nullopt);
            level_instance_->Unload();
            const LevelInstanceResult level_result =
                level_instance_->Instantiate(startup_level_asset_);
            if (!level_result)
            {
                return {false, "Startup level reload failed: " + level_result.diagnostic};
            }

            const std::optional<gameplay::ActorHandle> camera_handle =
                level_instance_->GetPreferredCameraActor();
            if (!camera_handle.has_value())
            {
                level_instance_->Unload();
                return {false, "Reloaded startup level has no enabled camera"};
            }

            bool controller_ready = false;
            if (startup_controller_setup_override_)
            {
                controller_ready = startup_controller_setup_override_(
                    *gameplay_world_, input_system_.get(), *camera_handle);
            }
            else
            {
                controller_ready = gameplay_world_->GetLocalPlayerController()->Possess(
                    *camera_handle);
            }
            if (!controller_ready)
            {
                level_instance_->Unload();
                return {false, "Startup controller could not possess the reloaded camera"};
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

        RuntimeContext::StartupResult RuntimeContext::PrepareRenderAssets(
            const std::vector<asset::AssetID> &roots)
        {
            EnsureSceneServices();
            const RenderAssetPreparationResult result =
                RenderAssetPreparer{}.Prepare(roots, graphics_api_type_);
            if (!result)
            {
                return {false, result.diagnostic};
            }
            prepared_render_assets_ = result.catalog;
            return {true, {}};
        }

        RuntimeContext::StartupResult RuntimeContext::QueueRenderAssetsReplacement(
            const std::vector<asset::AssetID> &roots, uint64_t &serial)
        {
            const StartupResult prepared = PrepareRenderAssets(roots);
            if (!prepared) return prepared;
            if (!render_system_)
            {
                return {false, "RenderSystem is unavailable for catalog replacement"};
            }
            serial = render_system_->QueuePreparedAssetsUpdate(prepared_render_assets_);
            if (serial == 0)
            {
                return {false, "RenderSystem rejected the prepared catalog replacement"};
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
            gameplay_command_registrations_ = {};
            report_progress(1, "Releasing level");
            level_instance_.reset();
            // With the level: both are consumers of Asset state, and releasing the copy
            // that describes that state before anything unloads is the safe order.
            asset_catalog_provider_.reset();
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
            level_reload_command_registration_ = {};
            path_trace_probe_command_registration_ = {};
            path_trace_settings_command_registration_ = {};
            path_trace_failure_command_registration_ = {};
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

        asset::IAssetCatalogSnapshotSource *RuntimeContext::GetAssetCatalogSnapshotSource() noexcept
        {
            // The concrete provider never appears in this signature, so an Editor header
            // including this one still cannot see or name it.
            return asset_catalog_provider_.get();
        }

    }
}
