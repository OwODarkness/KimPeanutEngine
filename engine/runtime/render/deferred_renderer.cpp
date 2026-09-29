#include "deferred_renderer.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <functional>
#include <span>
#include <stdexcept>

#include "graphics/backend/common/render_backend.h"
#include "graphics/backend/common/command_recorder.h"
#include "log/logger.h"
#include "render/camera_utils.h"
#include "render/material/material_system.h"
#include "render/material/material_asset_resolver.h"
#include "render/render_capture_service_internal.h"
#include "render/render_world/scene_visibility.h"
#include "render_resource_resolver.h"
#include "render_scene_coordinator.h"

namespace kpengine::render
{
    static_assert(static_cast<uint8_t>(RenderProfilePass::Count) ==
                  static_cast<uint8_t>(FixedRenderPassId::Count));

    namespace
    {
        static_assert(static_cast<size_t>(FixedRenderPassId::RayTracingPathTrace) ==
                      static_cast<size_t>(RenderProfilePass::RayTracingPathTrace));
        static_assert(static_cast<size_t>(FixedRenderPassId::Count) ==
                      static_cast<size_t>(RenderProfilePass::Count));

    }

    DeferredRenderer::~DeferredRenderer()
    {
        Cleanup();
    }

    DeferredRendererInitResult DeferredRenderer::Initialize(const DeferredRendererInitInfo &info,
                                                            uint32_t width, uint32_t height)
    {
        if (backend_ != nullptr)
        {
            return {false, "DeferredRenderer can only be initialized once."};
        }
        if (width == 0 || height == 0)
        {
            return {false, "DeferredRenderer requires a non-zero render extent."};
        }

        backend_ = &info.backend;
        resource_resolver_ = &info.resource_resolver;
        material_system_ = &info.materials;
        prepared_assets_ = &info.prepared_assets;
        ray_tracing_enabled_ = info.ray_tracing_enabled;
        path_tracing_enabled_ = info.ray_tracing_enabled && info.path_tracing_enabled;
        try
        {
            KP_LOG("RenderLog", LOG_LEVEL_INFO, "R4.6 initializing frame targets");
            frame_targets_.Initialize(*backend_, width, height);
            if (!frame_targets_.IsValid())
            {
                throw std::runtime_error("Failed to create the complete render target set.");
            }
            KP_LOG("RenderLog", LOG_LEVEL_INFO, "R4.6 frame targets initialized");
            if (ray_tracing_enabled_ && path_tracing_enabled_ &&
                backend_->GetCapabilities().SupportsRayTracingPipeline())
                path_tracing_pass_.PrepareResources(*backend_, *prepared_assets_);
            KP_LOG("RenderLog", LOG_LEVEL_INFO,
                   "R4.6 path tracing resource preparation completed");
            ConfigureFramePlans();
            if (!frame_plan_valid_)
            {
                throw std::runtime_error("Render graph frame plan compilation failed.");
            }
            return {true, {}};
        }
        catch (const std::exception &error)
        {
            const std::string diagnostic = error.what();
            Cleanup();
            return {false, diagnostic};
        }
        catch (...)
        {
            Cleanup();
            return {false, "Unknown exception during DeferredRenderer initialization."};
        }
    }

    void DeferredRenderer::Cleanup()
    {
        const uint32_t allocated_history_targets =
            path_tracing_pass_.GetHistoryTargetCount();
        // Release graph-owned leases before the backend tears its pool down.
        graph_executor_.Abort();
        frame_bindings_.Clear();
        ClearActiveFrameInputs();
        if (backend_ != nullptr)
            path_tracing_pass_.ReleaseBindings(*backend_);
        ray_tracing_scene_.Cleanup(backend_);
        // RT bindings and acceleration structures are retired against submitted
        // work; wait before releasing their referenced targets or pipeline.
        if (backend_ != nullptr)
            backend_->WaitIdle();
        if (backend_ != nullptr)
            path_tracing_pass_.Cleanup(*backend_);
        const uint32_t remaining_history_targets =
            path_tracing_pass_.GetHistoryTargetCount();
        KP_LOG("RenderLog", LOG_LEVEL_INFO,
               "R4.6 history-target teardown: released=%u, remaining=%u",
               allocated_history_targets - remaining_history_targets,
               remaining_history_targets);
        if (backend_ != nullptr)
        {
            deferred_lighting_pass_.Cleanup(*backend_);
            tone_map_pass_.Cleanup(*backend_);
            capture_view_pass_.Cleanup(*backend_);
            fullscreen_pass_resources_.Cleanup(*backend_);
            shadow_pass_.Cleanup(*backend_);
        }
        deferred_lighting_pass_.ClearEnvironment();
        shadow_pass_.BeginFrame();
        scene_draw_recorder_.Clear();
        ray_tracing_scene_.CancelPreparedBuildResources(
            backend_ != nullptr ? backend_->GetRayTracingResourceOwner() : nullptr);
        pending_scene_render_target_extent_ = {};
        graph_executor_.Abort();
        frame_bindings_.Clear();
        for (std::optional<CompiledRenderFramePlan> &plan : frame_plans_)
        {
            plan.reset();
        }
        frame_plan_valid_ = false;
        frame_plan_compile_ms_ = 0.0;
        frame_targets_.Cleanup();
        backend_ = nullptr;
        resource_resolver_ = nullptr;
        material_system_ = nullptr;
        prepared_assets_ = nullptr;
    }

    void DeferredRenderer::RequestExtent(uint32_t width, uint32_t height)
    {
        if (width != 0 && height != 0)
        {
            pending_scene_render_target_extent_ = {width, height};
        }
    }

    void DeferredRenderer::ApplyPendingExtent()
    {
        ApplyPendingSceneRenderTargetExtent();
    }

    const RenderTarget &DeferredRenderer::GetSceneRenderTarget() const
    {
        static const RenderTarget empty_target;
        const RenderTarget *const scene_target =
            frame_targets_.GetTarget(RenderTargetName::SceneColor);
        return scene_target ? *scene_target : empty_target;
    }

    spatial::Ray DeferredRenderer::BuildSceneRay(float ndc_x, float ndc_y,
                                                  float viewport_aspect) const
    {
        return scene_camera_.BuildWorldRay(ndc_x, ndc_y, viewport_aspect);
    }

    std::optional<Vector3f> DeferredRenderer::ProjectScenePoint(
        const Vector3f &world_point, float viewport_aspect) const
    {
        if (viewport_aspect <= 0.0f)
        {
            return std::nullopt;
        }

        RenderCamera camera = scene_camera_;
        camera.SetAspect(viewport_aspect);
        const Vector4f clip = camera.GetViewProjectionMatrix() * Vector4f(world_point, 1.0f);
        if (clip.w_ <= 0.0001f)
        {
            return std::nullopt;
        }

        const float inverse_w = 1.0f / clip.w_;
        return Vector3f{clip.x_ * inverse_w, clip.y_ * inverse_w, clip.z_ * inverse_w};
    }

    graphics::RenderTargetView DeferredRenderer::GetViewportRenderTargetView(
        CaptureView view) const
    {
        const RenderTargetName target_name = view == CaptureView::SceneColor
                                                 ? RenderTargetName::SceneColor
                                                 : RenderTargetName::DebugViewOutput;
        const RenderTarget *const target = frame_targets_.GetTarget(target_name);
        return target ? target->GetView() : graphics::RenderTargetView{};
    }

    graphics::RenderTargetHandle DeferredRenderer::GetCaptureTarget(CaptureView view) const
    {
        const RenderTargetName target_name = view == CaptureView::SceneColor
                                                 ? RenderTargetName::SceneColor
                                                 : RenderTargetName::CaptureOutput;
        const RenderTarget *const target = frame_targets_.GetTarget(target_name);
        return target ? target->GetHandle() : graphics::RenderTargetHandle{};
    }

    void DeferredRenderer::SetPathTraceSettings(const PathTraceSettings &settings)
    {
        if (!IsValidPathTraceSettings(settings))
        {
            return;
        }
        requested_path_trace_settings_ = settings;
        path_tracing_enabled_ = settings.path_tracing_enabled &&
                                path_tracing_pass_.Available();
    }

    void DeferredRenderer::InjectNextPathTraceDispatchFailure()
    {
        path_tracing_pass_.InjectNextDispatchFailure();
    }

    void DeferredRenderer::InvalidateRayTracingTextureBindings()
    {
        if (backend_ != nullptr)
            path_tracing_pass_.ReleaseBindings(*backend_);
    }

    DeferredRendererFrameResult DeferredRenderer::RecordFrame(
        FrameContext &frame_context, const RenderSceneFrameInput &input)
    {
        DeferredRendererFrameResult result{};
        triangle_count_ = 0;
        profile_ = {};
        profile_.graph_compile_ms = frame_plan_compile_ms_;
        profile_.frame_number = frame_context.GetGlobals().frame_number;
        profile_.last_required_graph_failure = last_required_graph_failure_;
        profile_.graphics_api = backend_->GetGraphicsAPI();
        profile_.ray_tracing_enabled = ray_tracing_enabled_;
        profile_.path_tracing_available = path_tracing_pass_.Available();
        profile_.ray_query_shadows_available =
            backend_->GetCapabilities().SupportsRayQueryShadows();
        profile_.viewport_width = frame_context.GetRenderExtent().width;
        profile_.viewport_height = frame_context.GetRenderExtent().height;
        effective_path_trace_settings_ = requested_path_trace_settings_;
        path_trace_settings_fallback_reason_.clear();
        effective_path_trace_settings_.path_tracing_enabled =
            requested_path_trace_settings_.path_tracing_enabled &&
            path_tracing_pass_.Available();
        if (requested_path_trace_settings_.path_tracing_enabled &&
            !path_tracing_pass_.Available())
        {
            path_trace_settings_fallback_reason_ =
                "Path tracing was requested but its pipeline/resources are unavailable.";
        }
        path_tracing_enabled_ = effective_path_trace_settings_.path_tracing_enabled;
        profile_.path_tracing_enabled = path_tracing_enabled_;
        profile_.path_trace_samples_per_dispatch =
            effective_path_trace_settings_.samples_per_dispatch;
        profile_.path_trace_max_continuation_bounces =
            effective_path_trace_settings_.maximum_continuation_bounces;
        profile_.path_trace_direct_light_sampling =
            effective_path_trace_settings_.direct_light_sampling;
        profile_.path_trace_settings_requested = requested_path_trace_settings_;
        profile_.path_trace_settings_effective = effective_path_trace_settings_;
        profile_.textures = resource_resolver_->GetTextureMetrics();
        material_system_->ResetProfileCounters();
        if (!frame_plan_valid_)
        {
            ClearActiveFrameInputs();
            result.normal_recording_completed = false;
            return result;
        }
        if (graph_executor_.IsActive())
        {
            result.normal_recording_completed = false;
            return result;
        }
        active_frame_context_ = &frame_context;
        render_world_ = &input.render_world;
        const uint64_t render_world_revision = render_world_->GetRevision();
        const auto render_world_snapshot_started = std::chrono::steady_clock::now();
        scene_draw_recorder_.BeginFrame(render_world_->Snapshot(), render_world_revision);
        profile_.cpu_render_world_snapshot_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - render_world_snapshot_started)
                .count();
        scene_camera_ = input.camera;
        pending_adaptive_spp_decision_.reset();
        path_trace_sampling_state_ = "fixed";
        if (effective_path_trace_settings_.path_tracing_enabled &&
            effective_path_trace_settings_.sampling_policy ==
                PathTraceSamplingPolicy::AdaptiveCameraMotion)
        {
            const Vector3f position = scene_camera_.GetPosition();
            const Rotatorf rotation = scene_camera_.GetRotation();
            const detail::PathTraceCameraMotionSample motion_sample{
                {position.x_, position.y_, position.z_},
                {rotation.pitch_, rotation.yaw_, rotation.roll_}};
            const detail::PathTraceAdaptiveSppState initial_state =
                path_trace_adaptive_sampling_active_ ? adaptive_spp_state_
                                                     : detail::PathTraceAdaptiveSppState{};
            pending_adaptive_spp_decision_ = detail::EvaluatePathTraceAdaptiveSpp(
                initial_state, motion_sample,
                effective_path_trace_settings_.camera_translation_threshold,
                effective_path_trace_settings_.camera_rotation_threshold_degrees,
                effective_path_trace_settings_.settle_frame_threshold);
            const bool camera_moving =
                pending_adaptive_spp_decision_->camera_moving;
            const detail::PathTraceAdaptiveSppSelection sampling_selection =
                detail::SelectPathTraceAdaptiveSpp(
                    camera_moving, path_tracing_pass_.SampleCount(),
                    effective_path_trace_settings_.quality_2spp_sample_threshold,
                    effective_path_trace_settings_.quality_1spp_sample_threshold,
                    effective_path_trace_settings_.moving_samples_per_dispatch,
                    effective_path_trace_settings_.settled_samples_per_dispatch,
                    effective_path_trace_settings_.quality_2spp_samples_per_dispatch,
                    effective_path_trace_settings_.quality_maintenance_samples_per_dispatch);
            effective_path_trace_settings_.samples_per_dispatch =
                sampling_selection.samples_per_dispatch;
            effective_path_trace_settings_.reconstruction =
                detail::SelectAdaptivePathTraceReconstruction(
                    camera_moving,
                    effective_path_trace_settings_.adaptive_moving_reconstruction);
            switch (sampling_selection.phase)
            {
            case detail::PathTraceAdaptiveSppPhase::Moving:
                path_trace_sampling_state_ = "moving";
                break;
            case detail::PathTraceAdaptiveSppPhase::ConvergingHighSpp:
                path_trace_sampling_state_ = "stable_accumulating_4spp";
                break;
            case detail::PathTraceAdaptiveSppPhase::ConvergingMediumSpp:
                path_trace_sampling_state_ = "stable_accumulating_2spp";
                break;
            case detail::PathTraceAdaptiveSppPhase::QualityMaintenance:
                path_trace_sampling_state_ = "quality_maintaining_1spp";
                break;
            }
        }
        profile_.path_trace_samples_per_dispatch =
            effective_path_trace_settings_.samples_per_dispatch;
        profile_.path_trace_settings_effective = effective_path_trace_settings_;
        profile_.path_trace_sampling_state = path_trace_sampling_state_;
        const Vector3f profile_camera_position = scene_camera_.GetPosition();
        profile_.path_trace_camera_position = {
            profile_camera_position.x_, profile_camera_position.y_,
            profile_camera_position.z_};
        // Readback and the Editor Viewer can request different conversions in
        // one frame, so each owns its view and output target independently.
        active_pending_capture_ =
            input.pending_capture.has_value() &&
                    RequiresCaptureViewConversionPass(*input.pending_capture)
                ? input.pending_capture
                : std::nullopt;
        active_debug_view_ = input.debug_view.has_value() &&
                                     RequiresCaptureViewConversionPass(*input.debug_view)
                                 ? input.debug_view
                                 : std::nullopt;
        const bool is_deferred_capture = active_pending_capture_.has_value();
        if (resource_resolver_ != nullptr && prepared_assets_ != nullptr)
            deferred_lighting_pass_.UpdateEnvironment(
                input.environment, input.environment_handle,
                *resource_resolver_, *prepared_assets_);
        else
            deferred_lighting_pass_.ClearEnvironment();
        shadow_pass_.BeginFrame();
        profile_.shadow_cache_hits = 0;
        profile_.shadow_cache_misses = 0;
        profile_.point_shadow_cache_hits = 0;
        profile_.point_shadow_cache_misses = 0;
        const auto ray_tracing_scene_prepare_started = std::chrono::steady_clock::now();
        const bool ray_tracing_scene_prepared =
            !ray_tracing_enabled_ ||
            (backend_ != nullptr && ray_tracing_scene_.Prepare(
                {*backend_, render_world_, scene_draw_recorder_, material_system_,
                 resource_resolver_, path_tracing_enabled_}));
        profile_.cpu_ray_tracing_scene_prepare_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - ray_tracing_scene_prepare_started)
                .count();
        if (!ray_tracing_scene_prepared)
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                   "Frame stopped because ray tracing scene preparation failed");
            ClearActiveFrameInputs();
            result.normal_recording_completed = false;
            return result;
        }
        ray_tracing_scene_.PrepareLights(input.lights, path_tracing_enabled_);
        const RayTracingSceneView rt_scene = ray_tracing_scene_.View();
        profile_.ray_tracing_geometry_records = static_cast<uint32_t>(
            rt_scene.geometries.size());
        profile_.ray_tracing_instance_records = static_cast<uint32_t>(
            rt_scene.path_instances.size());
        profile_.ray_tracing_material_records = static_cast<uint32_t>(
            rt_scene.path_materials.size());
        profile_.ray_tracing_light_records = static_cast<uint32_t>(
            rt_scene.path_lights.size());
        profile_.path_trace_environment_enabled = deferred_lighting_pass_.Environment().ibl_enabled;
        profile_.path_trace_environment_intensity = deferred_lighting_pass_.Environment().ibl_intensity;
        // The first loading frame can precede the first populated world
        // snapshot. Do not select an RT graph variant until its imported TLAS
        // provider exists; the next frame will rebuild the plan selection.
        const bool path_trace_scene_within_capacity =
            path_tracing_pass_.CanTraceScene(rt_scene);
        const bool has_path_trace_scene = !rt_scene.instances.empty();
        const bool has_path_trace_tlas = rt_scene.top_level.IsValid();
        const bool has_visible_mesh_proxy = std::any_of(
            scene_draw_recorder_.Snapshot().begin(), scene_draw_recorder_.Snapshot().end(),
            [](const MeshProxy &proxy) { return proxy.flags.visible && proxy.mesh.IsValid(); });
        const bool ray_query_capable =
            ray_tracing_enabled_ && backend_->GetCapabilities().SupportsRayQueryShadows();
        if (effective_path_trace_settings_.visibility_method ==
                PathTraceVisibilityMethod::RayQuery && !ray_query_capable)
        {
            effective_path_trace_settings_.visibility_method =
                PathTraceVisibilityMethod::RayPipeline;
            if (!path_trace_settings_fallback_reason_.empty())
            {
                path_trace_settings_fallback_reason_ += " ";
            }
            path_trace_settings_fallback_reason_ +=
                "Path-trace ray-query visibility is unavailable; ray-pipeline visibility is active.";
        }
        effective_path_trace_settings_.hybrid_ray_query_shadows_enabled =
            requested_path_trace_settings_.hybrid_ray_query_shadows_enabled &&
            ray_query_capable && has_path_trace_tlas && has_path_trace_scene &&
            deferred_lighting_pass_.HasRayQueryPipeline();
        if (requested_path_trace_settings_.hybrid_ray_query_shadows_enabled &&
            !effective_path_trace_settings_.hybrid_ray_query_shadows_enabled)
        {
            if (!path_trace_settings_fallback_reason_.empty())
            {
                path_trace_settings_fallback_reason_ += " ";
            }
            path_trace_settings_fallback_reason_ += ray_query_capable
                ? "Hybrid ray-query shadows await a valid scene TLAS."
                : "Hybrid ray-query shadows are unsupported; raster shadow maps remain active.";
        }
        profile_.path_trace_settings_effective = effective_path_trace_settings_;
        profile_.path_trace_settings_fallback_reason = path_trace_settings_fallback_reason_;
        const bool ray_query_shadow =
            effective_path_trace_settings_.hybrid_ray_query_shadows_enabled;
        if (path_tracing_pass_.Available() && has_visible_mesh_proxy &&
            (!has_path_trace_scene || !has_path_trace_tlas || !path_trace_scene_within_capacity))
        {
            const uint64_t diagnostic_signature =
                rt_scene.instance_signature ^
                (static_cast<uint64_t>(rt_scene.geometries.size()) << 32u) ^
                static_cast<uint64_t>(rt_scene.path_instances.size()) ^ 1u;
            if (path_tracing_pass_.ShouldReportSceneCapacity(diagnostic_signature))
            {
                KP_LOG("RenderLog", LOG_LEVEL_WARNING,
                       "Path tracing inactive: geometry_records=%zu instance_records=%zu "
                       "material_records=%zu light_records=%zu "
                       "TLAS_valid=%s (limit=%u)",
                       rt_scene.geometries.size(),
                       rt_scene.path_instances.size(),
                       rt_scene.path_materials.size(),
                       rt_scene.path_lights.size(),
                       has_path_trace_tlas ? "true" : "false",
                       path_tracing_pass_.MaximumSceneRecords());
            }
        }
        else
        {
            // An empty or not-yet-published render world is a normal loading
            // state; defer the inactivity diagnostic until there is scene work.
            path_tracing_pass_.ClearSceneCapacityReport();
        }
        const bool ray_tracing_path_trace = path_tracing_enabled_ &&
                                            path_tracing_pass_.Available() &&
                                            rt_scene.top_level.IsValid() &&
                                            has_path_trace_scene &&
                                            path_trace_scene_within_capacity;
        path_tracing_pass_.SetActive(ray_tracing_path_trace);
        profile_.path_trace_active = ray_tracing_path_trace;
        if (ray_tracing_path_trace)
        {
            if (backend_ == nullptr || prepared_assets_ == nullptr ||
                !fullscreen_pass_resources_.Initialize(*backend_) ||
                !tone_map_pass_.PrepareResources(*backend_, *prepared_assets_))
            {
                KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                       "Frame stopped because path tracing tone-map resources are unavailable");
                ClearActiveFrameInputs();
                result.normal_recording_completed = false;
                return result;
            }
            const graphics::Extent2D extent = frame_context.GetRenderExtent();
            if (!path_tracing_pass_.EnsureHistoryTargets(
                    *backend_, extent.width, extent.height))
            {
                KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                       "Frame stopped because path tracing history targets could not be prepared");
                ClearActiveFrameInputs();
                result.normal_recording_completed = false;
                return result;
            }
            const uint64_t signature = path_tracing_pass_.HistorySignature(
                extent.width, extent.height, rt_scene,
                deferred_lighting_pass_.Environment(), scene_camera_,
                effective_path_trace_settings_);
            if (const char *const reset_reason =
                    path_tracing_pass_.UpdateHistorySignature(signature))
                profile_.path_trace_history_reset_reason = reset_reason;
            profile_.path_trace_samples = path_tracing_pass_.SampleCount();
        }
        const RenderFrameConditions frame_conditions{
            is_deferred_capture, ray_query_shadow, rt_scene.blas_build_required,
            rt_scene.tlas_build_required, ray_tracing_path_trace,
            active_debug_view_.has_value()};
        const CompiledRenderFramePlan *const compiled_frame_plan =
            GetCompiledFramePlan(frame_conditions);
        const CompiledRenderGraph *const frame_plan =
            compiled_frame_plan != nullptr && compiled_frame_plan->compilation.graph.has_value()
                ? &*compiled_frame_plan->compilation.graph
                : nullptr;
        if (frame_plan == nullptr)
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                   "Frame condition set has no compiled render graph plan");
            ClearActiveFrameInputs();
            result.normal_recording_completed = false;
            return result;
        }
        const auto has_graph_pass = [frame_plan](FixedRenderPassId id) {
            return std::any_of(frame_plan->Passes().begin(), frame_plan->Passes().end(),
                               [id](const CompiledRenderGraph::Pass &pass) {
                                   return pass.user_key.has_value() &&
                                          *pass.user_key == static_cast<uint64_t>(id);
                               });
        };
        const bool needs_shadow_maps =
            has_graph_pass(FixedRenderPassId::DirectionalShadow) ||
            has_graph_pass(FixedRenderPassId::SpotShadow) ||
            has_graph_pass(FixedRenderPassId::PointShadow);
        if (needs_shadow_maps)
        {
            const auto shadow_stamp_fit_started = std::chrono::steady_clock::now();
            shadow_pass_.Schedule(input.lights, input.is_shadow_handle_valid,
                                  scene_draw_recorder_, *resource_resolver_,
                                  *material_system_, *render_world_,
                                  scene_camera_.GetPosition(), profile_);
            profile_.cpu_shadow_stamp_fit_ms +=
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - shadow_stamp_fit_started)
                    .count();
        }
        profile_.render_graph_mode = is_deferred_capture
                                         ? "capture"
                                         : ray_tracing_path_trace
                                               ? "path_tracing"
                                               : ray_query_shadow ? "hybrid_ray_query"
                                                                  : "deferred";
        graph_executor_.BeginFrame(*frame_plan);
        std::string transient_error;
        const graphics::Extent2D transient_extent = frame_context.GetRenderExtent();
        if (!graph_executor_.AcquireTransients(
                *frame_plan, *backend_, transient_extent,
                [this](uint64_t key, graphics::Extent2D extent) {
                    return DescribeFrameTransient(key, extent);
                },
                transient_error))
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR, "Frame graph transient acquisition failed: %s",
                   transient_error.c_str());
            graph_executor_.Abort();
            frame_bindings_.Clear();
            ClearActiveFrameInputs();
            // Deferred lighting writes it and tone map reads it, so a frame
            // without it cannot record.
            result.normal_recording_completed = false;
            return result;
        }
        if (!BuildFrameResourceBindings(*compiled_frame_plan))
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                   "Frame graph physical resources could not be bound before pass recording");
            CancelPreparedFrameBuildResources();
            graph_executor_.Abort();
            frame_bindings_.Clear();
            ClearActiveFrameInputs();
            result.normal_recording_completed = false;
            return result;
        }
        graphics::CommandRecorder *const graph_recorder = backend_->GetCommandRecorder();
        if (graph_recorder == nullptr)
        {
            CancelPreparedFrameBuildResources();
            graph_executor_.Abort();
            frame_bindings_.Clear();
            ClearActiveFrameInputs();
            result.normal_recording_completed = false;
            return result;
        }
        graph_executor_.BeginFrame(*frame_plan, frame_bindings_, *graph_recorder);
        frame_execution_failed_ = false;
        const auto graph_execute_started = std::chrono::steady_clock::now();
        const bool cursor_started = graph_executor_.ExecuteRenderer(
            [this](const CompiledRenderGraph::Pass &pass) {
                if (!pass.user_key.has_value())
                    return RenderGraphPassDisposition::Fail;
                const auto pass_id = static_cast<FixedRenderPassId>(*pass.user_key);
                if (pass_id == FixedRenderPassId::DirectionalShadow &&
                    shadow_pass_.DirectionalCacheHit())
                    return RenderGraphPassDisposition::ReusePreviousOutput;
                if (pass_id == FixedRenderPassId::PointShadow && shadow_pass_.PointCacheHit())
                {
                    shadow_pass_.MarkPointCacheReused();
                    return RenderGraphPassDisposition::ReusePreviousOutput;
                }
                return RenderGraphPassDisposition::Record;
            },
            [this, &input](const RenderGraphPassContext &context) {
                const CompiledRenderGraph::Pass &pass = context.GetPass();
                if (!pass.user_key.has_value())
                    return false;
                const auto pass_id = static_cast<FixedRenderPassId>(*pass.user_key);
                struct ScopedPassContext
                {
                    const RenderGraphPassContext *&slot;
                    explicit ScopedPassContext(const RenderGraphPassContext *&target,
                                               const RenderGraphPassContext &context)
                        : slot(target)
                    {
                        slot = &context;
                    }
                    ~ScopedPassContext() { slot = nullptr; }
                } scoped_context(active_pass_context_, context);
                return ExecutePass(pass_id, input.lights, context);
            },
            [this](uint64_t key) {
                backend_->BeginGpuProfilePass(GetRenderGpuProfilePassId(
                    static_cast<RenderProfilePass>(key)));
            },
            [this](uint64_t key) {
                backend_->EndGpuProfilePass(GetRenderGpuProfilePassId(
                    static_cast<RenderProfilePass>(key)));
            });
        profile_.cpu_graph_execute_ms +=
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - graph_execute_started)
                .count();
        result.normal_recording_completed =
            cursor_started && !graph_executor_.HasRequiredFailure();
        if (input.pending_capture.has_value())
        {
            result.capture_target_ready =
                input.pending_capture.value() == CaptureView::SceneColor ||
                !RequiresCaptureViewConversionPass(*input.pending_capture) ||
                graph_executor_.GetOutcome(
                    static_cast<uint64_t>(FixedRenderPassId::CaptureView)) ==
                    RenderGraphPassOutcome::Executed;
        }
        const MaterialProfileCounters material_profile =
            material_system_->GetProfileCounters();
        const FrameContextProfileCounters frame_profile =
            frame_context.GetProfileCounters();
        profile_.cpu_material_resolution_ms += material_profile.resolution_cpu_ms;
        profile_.material_resolution_calls += material_profile.resolution_calls;
        profile_.cpu_material_resolution_ms += frame_profile.material_resolution_cpu_ms;
        profile_.cpu_uniform_write_ms += frame_profile.uniform_write_cpu_ms;
        profile_.uniform_writes += frame_profile.uniform_writes;
        profile_.uniform_write_bytes += frame_profile.uniform_write_bytes;
        const SceneDrawProfileCounters &draw_profile =
            scene_draw_recorder_.GetProfileCounters();
        profile_.cpu_section_packet_build_ms =
            draw_profile.section_packet_build_cpu_ms;
        profile_.section_packet_build_calls = draw_profile.section_packet_build_calls;
        profile_.section_packets_built = draw_profile.section_packets_built;
        deferred_lighting_pass_.ClearFrameLightingBinding();
        return result;
    }

    bool DeferredRenderer::ExecutePass(FixedRenderPassId id, const std::vector<Light> &lights,
                                       const RenderGraphPassContext &context)
    {
        const size_t profile_index = static_cast<size_t>(id);
        const auto started = std::chrono::steady_clock::now();
        struct ActiveProfilePassScope
        {
            std::optional<size_t> &slot;
            explicit ActiveProfilePassScope(std::optional<size_t> &target,
                                            size_t index) noexcept
                : slot(target)
            {
                slot = index;
            }
            ~ActiveProfilePassScope() { slot.reset(); }
        } active_profile_pass_scope(active_profile_pass_, profile_index);
        bool succeeded = false;
        switch (id)
        {
        case FixedRenderPassId::DirectionalShadow:
            succeeded = RecordDirectionalShadowPass();
            break;
        case FixedRenderPassId::SpotShadow:
            succeeded = RecordSpotShadowPass();
            break;
        case FixedRenderPassId::PointShadow:
            succeeded = RecordPointShadowPass();
            break;
        case FixedRenderPassId::GBuffer:
            succeeded = RecordGBufferPass();
            break;
        case FixedRenderPassId::DeferredLighting:
        {
            ResolvedLightShadowBindings resolved_shadows;
            if (shadow_pass_.DirectionalFrame().has_value())
            {
                const DirectionalShadowFrame &shadow = *shadow_pass_.DirectionalFrame();
                resolved_shadows.push_back({shadow.job.source_light, shadow.shadow,
                                            shadow.job.kind, shadow.job.binding_slot});
            }
            if (shadow_pass_.SpotFrame().has_value() && shadow_pass_.SpotRecorded())
            {
                const SpotShadowFrame &shadow = *shadow_pass_.SpotFrame();
                resolved_shadows.push_back({shadow.job.source_light, shadow.shadow,
                                            shadow.job.kind, shadow.job.binding_slot});
            }
            if (shadow_pass_.PointFrame().has_value() && shadow_pass_.PointRecorded())
            {
                const PointShadowFrame &shadow = *shadow_pass_.PointFrame();
                resolved_shadows.push_back({shadow.job.source_light, shadow.shadow,
                                            shadow.job.kind, shadow.job.binding_slot});
            }
            deferred_lighting_pass_.SetFrameLightingBinding(active_frame_context_->CreateLightingBinding(
                BuildLightGpuFrameData(lights, resolved_shadows)));
            succeeded = RecordDeferredLightingPass();
            break;
        }
        case FixedRenderPassId::ToneMap:
            succeeded = RecordToneMapPass();
            break;
        case FixedRenderPassId::RayTracingToneMap:
            succeeded = RecordToneMapPass();
            break;
        case FixedRenderPassId::RayTracingPathTrace:
            succeeded = RecordRayTracingPathTracePass();
            break;
        case FixedRenderPassId::CaptureView:
            succeeded = active_pending_capture_.has_value() &&
                        RecordCaptureViewPass(*active_pending_capture_,
                                              RenderTargetName::CaptureOutput);
            break;
        case FixedRenderPassId::DebugView:
            succeeded = active_debug_view_.has_value() &&
                        RecordCaptureViewPass(*active_debug_view_,
                                              RenderTargetName::DebugViewOutput);
            break;
        case FixedRenderPassId::EditorComposite:
            succeeded = false;
            break;
        case FixedRenderPassId::RayTracingBlasBuild:
            succeeded = ray_tracing_scene_.RecordBlasBuild(context);
            break;
        case FixedRenderPassId::RayTracingTlasBuild:
            succeeded = ray_tracing_scene_.RecordTlasBuild(context);
            break;
        case FixedRenderPassId::Count:
            succeeded = false;
            break;
        }
        profile_.passes[profile_index].cpu_time_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started)
                .count();
        return succeeded;
    }

    bool DeferredRenderer::ExecuteEditorCompositePass(const std::function<void()> &record_pass)
    {
        if (!graph_executor_.IsActive())
        {
            return false;
        }
        if (!graph_executor_.CanExecuteExternal())
            return false;
        const auto started = std::chrono::steady_clock::now();
        const bool succeeded = graph_executor_.ExecuteExternal(
            record_pass,
            [this]() {
                backend_->BeginGpuProfilePass(
                    GetRenderGpuProfilePassId(RenderProfilePass::EditorComposite));
            },
            [this]() {
                backend_->EndGpuProfilePass(
                    GetRenderGpuProfilePassId(RenderProfilePass::EditorComposite));
            });
        if (!succeeded)
        {
            frame_execution_failed_ = true;
            KP_LOG("RenderLog", LOG_LEVEL_ERROR, "%s",
                   graph_executor_.GetDiagnostic().c_str());
        }
        profile_.passes[static_cast<size_t>(RenderProfilePass::EditorComposite)].cpu_time_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - started)
                .count();
        return succeeded;
    }

    bool DeferredRenderer::FinalizeFrame()
    {
        if (!graph_executor_.IsActive())
        {
            return false;
        }
        RenderGraphExecutionResult graph_result = graph_executor_.Finalize();
        const bool finalized = graph_result.finalized;
        for (size_t index = 0; index < profile_.graph_pass_outcomes.size(); ++index)
        {
            const auto outcome = std::find_if(
                graph_result.pass_outcomes.begin(), graph_result.pass_outcomes.end(),
                [index](const auto &entry) { return entry.first == index; });
            profile_.graph_pass_outcomes[index] = outcome != graph_result.pass_outcomes.end()
                ? outcome->second
                : RenderGraphPassOutcome::NotInPlan;
        }
        if (!graph_result.Succeeded() && !graph_result.diagnostic.empty())
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR, "Render graph frame failed: %s",
                   graph_result.diagnostic.c_str());
        }
        if (frame_execution_failed_)
        {
            if (graph_result.diagnostic.empty())
            {
                graph_result.diagnostic = "Render graph external pass requirements failed.";
            }
            KP_LOG("RenderLog", LOG_LEVEL_ERROR, "%s", graph_result.diagnostic.c_str());
        }
        const bool required_pass_failed = graph_result.required_pass_failed;
        const bool succeeded = graph_result.Succeeded() && !frame_execution_failed_;
        path_tracing_pass_.CommitFrame(
            finalized, frame_execution_failed_, required_pass_failed,
            effective_path_trace_settings_.samples_per_dispatch);
        if (succeeded && path_tracing_pass_.Active() &&
            pending_adaptive_spp_decision_.has_value())
        {
            adaptive_spp_state_ = pending_adaptive_spp_decision_->next_state;
            path_trace_adaptive_sampling_active_ = true;
        }
        else if (succeeded && path_tracing_pass_.Active())
        {
            path_trace_adaptive_sampling_active_ = false;
            adaptive_spp_state_ = {};
        }
        pending_adaptive_spp_decision_.reset();
        if (!succeeded)
        {
            last_required_graph_failure_.valid = true;
            last_required_graph_failure_.frame_number = profile_.frame_number;
            last_required_graph_failure_.path_trace_samples =
                path_tracing_pass_.SampleCount();
            last_required_graph_failure_.pass_outcomes = profile_.graph_pass_outcomes;
            profile_.last_required_graph_failure = last_required_graph_failure_;
        }
        if (succeeded && path_tracing_pass_.Active())
        {
            profile_.path_trace_samples = path_tracing_pass_.SampleCount();
        }
        CancelPreparedFrameBuildResources();
        if (graph_executor_.IsActive())
        {
            // Released after the sweep, so the next frame takes the same
            // instance back rather than a second one: the caller's descriptor
            // sets are keyed on this target's handles.
            graph_executor_.Abort();
            frame_bindings_.Clear();
            ClearActiveFrameInputs();
        }
        return succeeded;
    }

    RenderTarget *DeferredRenderer::ResolveFrameTexture(
        RenderFrameResourceRole role, RenderGraphAccess access) const
    {
        return active_pass_context_ != nullptr
                   ? active_pass_context_->ResolveTexture(role, access)
                   : nullptr;
    }

    void DeferredRenderer::ClearActiveFrameInputs()
    {
        active_frame_context_ = nullptr;
        render_world_ = nullptr;
        active_pass_context_ = nullptr;
        active_pending_capture_.reset();
        active_debug_view_.reset();
        deferred_lighting_pass_.ClearFrameLightingBinding();
    }

    void DeferredRenderer::CancelPreparedFrameBuildResources()
    {
        graphics::RayTracingResourceOwner *const owner =
            backend_ != nullptr ? backend_->GetRayTracingResourceOwner() : nullptr;
        ray_tracing_scene_.CancelPreparedBuildResources(owner);
    }

    bool DeferredRenderer::BuildFrameResourceBindings(const CompiledRenderFramePlan &plan)
    {
        if (!plan.compilation.graph.has_value())
        {
            return false;
        }
        frame_bindings_.Clear();
        std::string error;
        graphics::RayTracingResourceOwner *const ray_tracing_owner =
            backend_ != nullptr ? backend_->GetRayTracingResourceOwner() : nullptr;
        struct PreparedResourceRollback
        {
            graphics::RayTracingResourceOwner *owner;
            RayTracingScene &scene;
            bool keep = false;
            ~PreparedResourceRollback()
            {
                if (!keep)
                    scene.CancelPreparedBuildResources(owner);
            }
        } rollback{ray_tracing_owner, ray_tracing_scene_};
        const RayTracingSceneView pending_scene = ray_tracing_scene_.View();
        if ((!pending_scene.bottom_level_builds.empty() ||
             !pending_scene.top_level_builds.empty()) &&
            ray_tracing_owner == nullptr)
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                   "Ray-tracing builds have no Graphics owner");
            return false;
        }
        if (ray_tracing_owner != nullptr &&
            !ray_tracing_scene_.PrepareBuildResources(*ray_tracing_owner, profile_.frame_number))
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                   "Graphics could not prepare ray-tracing build resources");
            return false;
        }
        // Preparation rebuilds the owner's frame vectors; take spans only after it completes.
        const RayTracingSceneView scene = ray_tracing_scene_.View();
        const auto &blas_resources = ray_tracing_scene_.BlasResources();
        const auto &tlas_resources = ray_tracing_scene_.TlasResources();
        for (const RenderFrameResourceImport &import : plan.resources)
        {
            if (std::holds_alternative<GraphTextureHandle>(import.handle))
            {
                RenderTarget *target = nullptr;
                switch (import.role)
                {
                case RenderFrameResourceRole::SceneHdr:
                    target = path_tracing_pass_.Active()
                                 ? path_tracing_pass_.HistoryTarget(
                                       path_tracing_pass_.WriteIndex())
                                 : graph_executor_.ResolveTransient(
                                       static_cast<uint64_t>(RenderFrameTransient::SceneHdr));
                    break;
                case RenderFrameResourceRole::PathTraceHistory:
                    target = path_tracing_pass_.HistoryTarget(
                        1u - path_tracing_pass_.WriteIndex());
                    break;
                case RenderFrameResourceRole::PathTraceGuide:
                    target = path_tracing_pass_.GuideTarget();
                    break;
                case RenderFrameResourceRole::SceneColor:
                    target = frame_targets_.GetTarget(RenderTargetName::SceneColor);
                    break;
                case RenderFrameResourceRole::GBuffer:
                    target = frame_targets_.GetTarget(RenderTargetName::GBuffer);
                    break;
                case RenderFrameResourceRole::DirectionalShadow:
                    target = frame_targets_.GetTarget(RenderTargetName::DirectionalShadow);
                    break;
                case RenderFrameResourceRole::SpotShadow:
                    target = frame_targets_.GetTarget(RenderTargetName::SpotShadow);
                    break;
                case RenderFrameResourceRole::PointShadow:
                    target = frame_targets_.GetTarget(RenderTargetName::PointShadow);
                    break;
                case RenderFrameResourceRole::CaptureOutput:
                    target = frame_targets_.GetTarget(RenderTargetName::CaptureOutput);
                    break;
                case RenderFrameResourceRole::DebugViewOutput:
                    target = frame_targets_.GetTarget(RenderTargetName::DebugViewOutput);
                    break;
                default:
                    break;
                }
                if (!frame_bindings_.AddTexture(import.handle, import.role, target, error))
                {
                    KP_LOG("RenderLog", LOG_LEVEL_ERROR, "Texture binding failed: %s", error.c_str());
                    frame_bindings_.Clear();
                    return false;
                }
                continue;
            }

            if (std::holds_alternative<GraphBufferHandle>(import.handle))
            {
                std::vector<graphics::BufferHandle> buffers;
                if (import.role == RenderFrameResourceRole::SceneGeometry)
                {
                    for (const graphics::RayTracingGeometryDesc &geometry : scene.geometries)
                    {
                        buffers.push_back(geometry.vertex_buffer);
                        buffers.push_back(geometry.index_buffer);
                    }
                }
                else if (import.role == RenderFrameResourceRole::SceneInstances &&
                         tlas_resources.has_value())
                {
                    buffers = tlas_resources->instance_inputs;
                }
                else if (import.role == RenderFrameResourceRole::SceneScratch)
                {
                    if (blas_resources.has_value())
                    {
                        buffers.insert(buffers.end(),
                                       blas_resources->scratch_buffers.begin(),
                                       blas_resources->scratch_buffers.end());
                    }
                    if (tlas_resources.has_value())
                    {
                        buffers.insert(buffers.end(),
                                       tlas_resources->scratch_buffers.begin(),
                                       tlas_resources->scratch_buffers.end());
                    }
                }
                if (!frame_bindings_.AddBuffers(import.handle, import.role, buffers, error))
                {
                    KP_LOG("RenderLog", LOG_LEVEL_ERROR, "Buffer binding failed for role %u: %s",
                           static_cast<unsigned>(import.role), error.c_str());
                    frame_bindings_.Clear();
                    return false;
                }
                continue;
            }

            std::vector<graphics::AccelerationStructureHandle> structures;
            if (import.role == RenderFrameResourceRole::SceneBlas)
            {
                structures.assign(scene.bottom_levels.begin(), scene.bottom_levels.end());
            }
            else if (import.role == RenderFrameResourceRole::SceneTlas)
            {
                structures.push_back(
                    scene.top_level.IsValid()
                        ? scene.top_level
                        : (backend_ != nullptr
                               ? backend_->GetActiveTopLevelAccelerationStructure()
                               : graphics::AccelerationStructureHandle{}));
            }
            if (!frame_bindings_.AddAccelerationStructures(import.handle, import.role,
                                                            structures, error))
            {
                KP_LOG("RenderLog", LOG_LEVEL_ERROR, "AS binding failed: %s", error.c_str());
                frame_bindings_.Clear();
                return false;
            }
        }
        if (!frame_bindings_.Validate(plan, error))
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR, "Frame binding coverage failed: %s", error.c_str());
            frame_bindings_.Clear();
            return false;
        }
        if (path_tracing_pass_.Active())
        {
            const auto geometry_import = std::find_if(
                plan.resources.begin(), plan.resources.end(), [](const auto &import) {
                    return import.role == RenderFrameResourceRole::SceneGeometry &&
                           std::holds_alternative<GraphBufferHandle>(import.handle);
                });
            graphics::RayTracingResourceOwner *const owner =
                backend_ != nullptr ? backend_->GetRayTracingResourceOwner() : nullptr;
            const auto geometry = geometry_import != plan.resources.end()
                ? frame_bindings_.ResolveBuffers(
                      std::get<GraphBufferHandle>(geometry_import->handle))
                : std::span<const graphics::BufferHandle>{};
            if (owner == nullptr || geometry.empty())
            {
                KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                       "Path tracing reference table has no declared geometry provider");
                frame_bindings_.Clear();
                return false;
            }
            const RayTracingSceneTableUpdate table_update =
                ray_tracing_scene_.EnsureReferenceTable(*owner, geometry);
            if (!table_update.succeeded || !table_update.table.IsValid())
            {
                KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                       "Path tracing reference table preparation failed");
                frame_bindings_.Clear();
                return false;
            }
            profile_.ray_tracing_scene_table_records_packed = table_update.records_packed;
            profile_.ray_tracing_scene_table_records_uploaded = table_update.records_uploaded;
            profile_.cpu_ray_tracing_scene_table_pack_ms = table_update.cpu_pack_ms;
        }
        rollback.keep = true;
        return true;
    }

    std::optional<graphics::RenderTargetDesc> DeferredRenderer::DescribeFrameTransient(
        uint64_t key, const graphics::Extent2D &extent) const
    {
        if (key == static_cast<uint64_t>(RenderFrameTransient::SceneHdr))
        {
            return RendererFrameTargets::DescribeSceneHdr(extent.width, extent.height);
        }
        return std::nullopt;
    }

    void DeferredRenderer::ConfigureFramePlans()
    {
        // The declaration is static, so each condition variant is compiled once
        // here and reused for every frame that selects it.
        frame_plan_valid_ = true;
        frame_plan_compile_ms_ = 0.0;
        for (uint32_t condition_bits = 0; condition_bits < frame_plans_.size();
             ++condition_bits)
        {
            const RenderFrameConditions conditions{
                (condition_bits & 1U) != 0,
                (condition_bits & 2U) != 0,
                (condition_bits & 4U) != 0,
                (condition_bits & 8U) != 0,
                (condition_bits & 16U) != 0,
                (condition_bits & 32U) != 0};
            const std::size_t slot =
                (conditions.diagnostic_capture ? 1U : 0U) |
                (conditions.ray_query_shadow ? 2U : 0U) |
                (conditions.ray_tracing_blas_build ? 4U : 0U) |
                (conditions.ray_tracing_tlas_build ? 8U : 0U) |
                (conditions.ray_tracing_path_trace ? 16U : 0U) |
                (conditions.debug_view ? 32U : 0U);
            const auto started = std::chrono::steady_clock::now();
            frame_plans_[slot] = CompileRenderFramePlan(conditions);
            frame_plan_compile_ms_ += std::chrono::duration<double, std::milli>(
                                          std::chrono::steady_clock::now() - started)
                                          .count();
            if (frame_plans_[slot]->compilation.graph.has_value())
            {
                continue;
            }
            frame_plan_valid_ = false;
            for (const RenderGraphDiagnostic &diagnostic :
                 frame_plans_[slot]->compilation.diagnostics)
            {
                KP_LOG("RenderLog", LOG_LEVEL_ERROR, "Render graph declaration is invalid: %s",
                       diagnostic.message.c_str());
            }
        }
    }

    const CompiledRenderFramePlan *DeferredRenderer::GetCompiledFramePlan(
        RenderFrameConditions conditions) const
    {
        const std::optional<CompiledRenderFramePlan> &plan =
            frame_plans_[(conditions.diagnostic_capture ? 1U : 0U) |
                        (conditions.ray_query_shadow ? 2U : 0U) |
                        (conditions.ray_tracing_blas_build ? 4U : 0U) |
                        (conditions.ray_tracing_tlas_build ? 8U : 0U) |
                        (conditions.ray_tracing_path_trace ? 16U : 0U) |
                        (conditions.debug_view ? 32U : 0U)];
        if (!plan.has_value() || !plan->compilation.graph.has_value())
        {
            return nullptr;
        }
        return &*plan;
    }

    const CompiledRenderGraph *DeferredRenderer::GetFramePlan(
        RenderFrameConditions conditions) const
    {
        const CompiledRenderFramePlan *const plan = GetCompiledFramePlan(conditions);
        return plan != nullptr ? &*plan->compilation.graph : nullptr;
    }

    bool DeferredRenderer::RecordDirectionalShadowPass()
    {
        if (active_frame_context_ == nullptr)
            return false;
        if (shadow_pass_.DirectionalFrame().has_value() &&
            !shadow_pass_.PrepareDirectionalPipeline(*backend_, *prepared_assets_))
            return false;
        ShadowPassDrawCounts counts{};
        const bool succeeded = shadow_pass_.RecordDirectional(
            *backend_, ResolveFrameTexture(RenderFrameResourceRole::DirectionalShadow,
                                           RenderGraphAccess::Write),
            *active_frame_context_, *resource_resolver_, *material_system_,
            scene_draw_recorder_, counts);
        AddProfileDraws(counts.draw_calls, counts.sections);
        return succeeded;
    }

    bool DeferredRenderer::RecordSpotShadowPass()
    {
        if (active_frame_context_ == nullptr)
            return false;
        if (shadow_pass_.SpotFrame().has_value() &&
            !shadow_pass_.PrepareDirectionalPipeline(*backend_, *prepared_assets_))
            return false;
        ShadowPassDrawCounts counts{};
        const bool succeeded = shadow_pass_.RecordSpot(
            *backend_, ResolveFrameTexture(RenderFrameResourceRole::SpotShadow,
                                           RenderGraphAccess::Write),
            *active_frame_context_, *resource_resolver_, *material_system_,
            scene_draw_recorder_, counts);
        AddProfileDraws(counts.draw_calls, counts.sections);
        return succeeded;
    }

    bool DeferredRenderer::RecordPointShadowPass()
    {
        if (active_frame_context_ == nullptr)
            return false;
        if (shadow_pass_.PointFrame().has_value() &&
            !shadow_pass_.PrepareDirectionalPipeline(*backend_, *prepared_assets_))
            return false;
        ShadowPassDrawCounts counts{};
        const bool succeeded = shadow_pass_.RecordPoint(
            *backend_, ResolveFrameTexture(RenderFrameResourceRole::PointShadow,
                                           RenderGraphAccess::Write),
            *active_frame_context_, *resource_resolver_, scene_draw_recorder_, counts);
        AddProfileDraws(counts.draw_calls, counts.sections);
        return succeeded;
    }
    bool DeferredRenderer::RecordGBufferPass()
    {
        if (active_frame_context_ == nullptr || active_pass_context_ == nullptr)
            return false;
        const DeferredLightingRecordResult result = deferred_lighting_pass_.RecordGBuffer(
            *active_frame_context_, scene_camera_, scene_draw_recorder_,
            *material_system_, *resource_resolver_, active_pass_context_->GetRecorder(),
            ResolveFrameTexture(RenderFrameResourceRole::GBuffer,
                                RenderGraphAccess::Write));
        AddProfileDraws(result.draw_calls, result.sections);
        triangle_count_ += result.triangles;
        return result.succeeded;
    }
    bool DeferredRenderer::RecordDeferredLightingPass()
    {
        if (!active_frame_context_ || !active_pass_context_ ||
            !deferred_lighting_pass_.FrameLighting().IsValid() || backend_ == nullptr)
            return false;
        RenderTarget *const hdr_target = ResolveFrameTexture(
            RenderFrameResourceRole::SceneHdr, RenderGraphAccess::Write);
        RenderTarget *const gbuffer_target = ResolveFrameTexture(RenderFrameResourceRole::GBuffer);
        RenderTarget *const directional_target = ResolveFrameTexture(
            RenderFrameResourceRole::DirectionalShadow);
        RenderTarget *const spot_target = ResolveFrameTexture(RenderFrameResourceRole::SpotShadow);
        RenderTarget *const point_target = ResolveFrameTexture(RenderFrameResourceRole::PointShadow);
        if (!hdr_target || !gbuffer_target || !directional_target || !spot_target || !point_target)
            return false;
        if (!PrepareDeferredLightingPassResources())
            return false;

        graphics::AccelerationStructureHandle scene_tlas{};
        const bool ray_query_requested =
            effective_path_trace_settings_.hybrid_ray_query_shadows_enabled && ray_tracing_enabled_;
        if (ray_query_requested)
        {
            const auto handles = active_pass_context_->ResolveAccelerationStructures(
                RenderFrameResourceRole::SceneTlas, RenderGraphAccess::Read);
            if (handles.size() == 1)
                scene_tlas = handles.front();
        }
        const DeferredLightingFrameInputs inputs{
            deferred_lighting_pass_.FrameLighting(), deferred_lighting_pass_.Environment(), *gbuffer_target, *hdr_target,
            *directional_target, *spot_target, *point_target,
            shadow_pass_.DirectionalFrame() ? &*shadow_pass_.DirectionalFrame() : nullptr,
            shadow_pass_.SpotFrame() ? &*shadow_pass_.SpotFrame() : nullptr,
            shadow_pass_.PointFrame() ? &*shadow_pass_.PointFrame() : nullptr,
            shadow_pass_.PointRecorded(), shadow_pass_.DirectionalSampler(),
            shadow_pass_.SpotSampler(), shadow_pass_.PointSampler(), scene_tlas,
            ray_query_requested};
        const DeferredLightingRecordResult result = deferred_lighting_pass_.RecordLighting(
            *active_frame_context_, scene_camera_, inputs,
            fullscreen_pass_resources_, active_pass_context_->GetRecorder());
        profile_.ray_query_shadows_active = result.ray_query_shadows_active;
        AddProfileDraws(result.draw_calls, result.sections);
        return result.succeeded;
    }

    bool DeferredRenderer::PrepareDeferredLightingPassResources()
    {
        if (backend_ == nullptr || prepared_assets_ == nullptr)
            return false;
        const bool supports_ray_query = ray_tracing_enabled_ &&
            backend_->GetCapabilities().SupportsRayQueryShadows();
        if (deferred_lighting_pass_.ResourcesReady(supports_ray_query) &&
            fullscreen_pass_resources_.Mesh().IsValid() &&
            fullscreen_pass_resources_.LinearSampler().IsValid() &&
            shadow_pass_.DirectionalSampler().IsValid() &&
            shadow_pass_.SpotSampler().IsValid() && shadow_pass_.PointSampler().IsValid() &&
            deferred_lighting_pass_.Environment().HasCompleteBindings())
            return true;
        if (!fullscreen_pass_resources_.Initialize(*backend_) ||
            !shadow_pass_.PrepareSamplers(*backend_) || resource_resolver_ == nullptr ||
            !deferred_lighting_pass_.EnsureEnvironmentFallback(*resource_resolver_) ||
            !shadow_pass_.DirectionalSampler().IsValid() ||
            !shadow_pass_.SpotSampler().IsValid() || !shadow_pass_.PointSampler().IsValid() ||
            !deferred_lighting_pass_.Environment().HasCompleteBindings())
            return false;
        return deferred_lighting_pass_.PrepareResources(
            *backend_, *prepared_assets_, supports_ray_query);
    }

    bool DeferredRenderer::RecordRayTracingPathTracePass()
    {
        if (active_frame_context_ == nullptr || active_pass_context_ == nullptr ||
            backend_ == nullptr || resource_resolver_ == nullptr ||
            !deferred_lighting_pass_.EnsureEnvironmentFallback(*resource_resolver_))
            return false;
        return path_tracing_pass_.Record(
            *backend_, *active_frame_context_, scene_camera_,
            effective_path_trace_settings_,
            ray_tracing_scene_.View(), deferred_lighting_pass_.Environment(),
            *active_pass_context_);
    }
    bool DeferredRenderer::RecordToneMapPass()
    {
        if (active_frame_context_ == nullptr || active_pass_context_ == nullptr ||
            backend_ == nullptr || prepared_assets_ == nullptr)
            return false;
        RenderTarget *const hdr_target = ResolveFrameTexture(RenderFrameResourceRole::SceneHdr);
        RenderTarget *const gbuffer_target = path_tracing_pass_.Active()
            ? nullptr : ResolveFrameTexture(RenderFrameResourceRole::GBuffer);
        RenderTarget *const guide_target = path_tracing_pass_.Active()
            ? ResolveFrameTexture(RenderFrameResourceRole::PathTraceGuide) : nullptr;
        RenderTarget *const scene_target = ResolveFrameTexture(
            RenderFrameResourceRole::SceneColor, RenderGraphAccess::Write);
        if (hdr_target == nullptr || scene_target == nullptr ||
            (!path_tracing_pass_.Active() && gbuffer_target == nullptr) ||
            (path_tracing_pass_.Active() && guide_target == nullptr) ||
            !fullscreen_pass_resources_.Initialize(*backend_) ||
            !tone_map_pass_.PrepareResources(*backend_, *prepared_assets_))
            return false;
        const bool recorded = tone_map_pass_.Record(
            *active_frame_context_, *hdr_target, gbuffer_target, guide_target, *scene_target,
            fullscreen_pass_resources_, active_pass_context_->GetRecorder(),
            path_tracing_pass_.Active(), effective_path_trace_settings_,
            path_tracing_pass_.SampleCount());
        if (recorded)
            AddProfileDraws(1, 1);
        return recorded;
    }

    bool DeferredRenderer::RecordCaptureViewPass(CaptureView view,
                                                  RenderTargetName output_name)
    {
        if (active_frame_context_ == nullptr || active_pass_context_ == nullptr ||
            backend_ == nullptr || prepared_assets_ == nullptr || view == CaptureView::SceneColor)
            return false;
        RenderFrameResourceRole output_role{};
        if (output_name == RenderTargetName::CaptureOutput)
            output_role = RenderFrameResourceRole::CaptureOutput;
        else if (output_name == RenderTargetName::DebugViewOutput)
            output_role = RenderFrameResourceRole::DebugViewOutput;
        else
            return false;
        RenderTarget *const output = ResolveFrameTexture(output_role, RenderGraphAccess::Write);
        RenderTarget *const gbuffer = ResolveFrameTexture(RenderFrameResourceRole::GBuffer);
        RenderTarget *const directional = ResolveFrameTexture(
            RenderFrameResourceRole::DirectionalShadow);
        RenderTarget *const spot = ResolveFrameTexture(RenderFrameResourceRole::SpotShadow);
        RenderTarget *const point = ResolveFrameTexture(RenderFrameResourceRole::PointShadow);
        if (output == nullptr || gbuffer == nullptr || directional == nullptr ||
            spot == nullptr || point == nullptr ||
            !fullscreen_pass_resources_.Initialize(*backend_) ||
            !shadow_pass_.PrepareSamplers(*backend_) ||
            !capture_view_pass_.PrepareResources(*backend_, *prepared_assets_))
            return false;

        const CaptureViewFrameInputs inputs{
            *output, *gbuffer, *directional, *spot, *point,
            shadow_pass_.DirectionalFrame() ? &*shadow_pass_.DirectionalFrame() : nullptr,
            shadow_pass_.SpotFrame() ? &*shadow_pass_.SpotFrame() : nullptr,
            shadow_pass_.PointFrame() ? &*shadow_pass_.PointFrame() : nullptr,
            shadow_pass_.SpotRecorded(), shadow_pass_.PointRecorded(),
            fullscreen_pass_resources_.LinearSampler(), shadow_pass_.DirectionalSampler(),
            shadow_pass_.SpotSampler(), shadow_pass_.PointSampler()};
        const bool recorded = capture_view_pass_.Record(
            *active_frame_context_, scene_camera_, view, inputs, fullscreen_pass_resources_,
            active_pass_context_->GetRecorder());
        if (recorded)
            AddProfileDraws(1, 1);
        return recorded;
    }

    void DeferredRenderer::AddProfileDraws(const uint64_t draw_calls,
                                           const uint64_t sections)
    {
        profile_.draw_calls += draw_calls;
        profile_.sections += sections;
        if (active_profile_pass_.has_value())
        {
            RenderProfilePassMetrics &pass = profile_.passes[*active_profile_pass_];
            pass.draw_calls += draw_calls;
            pass.sections += sections;
        }
    }

    void DeferredRenderer::ApplyPendingSceneRenderTargetExtent()
    {
        if (!backend_ || pending_scene_render_target_extent_.width == 0 ||
            pending_scene_render_target_extent_.height == 0)
        {
            return;
        }

        const graphics::Extent2D requested = pending_scene_render_target_extent_;
        pending_scene_render_target_extent_ = {};
        const RenderTarget *const scene_target =
            frame_targets_.GetTarget(RenderTargetName::SceneColor);
        const bool extent_changed =
            scene_target == nullptr || scene_target->GetWidth() != requested.width ||
            scene_target->GetHeight() != requested.height;
        // RebuildForExtent retires via WaitIdle internally only when the extent
        // changed, so a stable size keeps the shared target's GPU generations
        // intact across frames. active_frame_context_ is nulled here to match the
        // old pre-rebuild boundary; the next BeginFrame re-acquires it.
        frame_targets_.RebuildForExtent(*backend_, requested.width, requested.height);
        if (extent_changed)
        {
            shadow_pass_.InvalidateCache();
            active_frame_context_ = nullptr;
        }
        if (!frame_targets_.GetTarget(RenderTargetName::SceneColor)->IsValid())
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                   "Failed to resize scene render target to %u x %u", requested.width,
                   requested.height);
        }
    }

}
