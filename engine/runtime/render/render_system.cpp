#include "render_system.h"

#include <atomic>
#include <stdexcept>
#include <chrono>
#include <utility>

#include "asset/mesh.h"
#include "graphics/backend/common/render_backend.h"
#include "log/logger.h"
#include "render/material/material_system.h"
#include "render/render_capture_service_internal.h"
#include "render_resource_resolver.h"

namespace kpengine::render
{
    namespace
    {
        constexpr const char *GetGraphicsApiName(GraphicsAPIType api_type)
        {
            switch (api_type)
            {
            case GraphicsAPIType::GRAPHICS_API_OPENGL:
                return "OpenGL";
            case GraphicsAPIType::GRAPHICS_API_VULKAN:
                return "Vulkan";
            case GraphicsAPIType::GRAPHICS_API_UNKNOW:
            default:
                return "Unknown";
            }
        }

    }

    RenderSystem::RenderSystem() = default;

    RenderSystem::~RenderSystem()
    {
        Shutdown();
    }

    RenderSystemInitResult RenderSystem::Initialize(const RenderSystemInitInfo &info)
    {
        const RenderSystemInitResult presentation_result = InitializePresentation(info);
        if (!presentation_result)
        {
            return presentation_result;
        }
        const RenderSystemInitResult scene_result = PromoteToScene(info.prepared_assets);
        if (!scene_result)
        {
            CleanupOwnedState();
            lifecycle_state_ = RenderSystemLifecycleState::Uninitialized;
        }
        return scene_result;
    }

    RenderSystemInitResult RenderSystem::InitializePresentation(
        const RenderSystemInitInfo &info)
    {
        if (lifecycle_state_ != RenderSystemLifecycleState::Uninitialized)
        {
            last_diagnostic_ = "RenderSystem can only be initialized once.";
            return {false, last_diagnostic_};
        }
        if (!info.native_window || !info.resize_dispatcher)
        {
            last_diagnostic_ =
                "RenderSystem presentation initialization requires window and resize dispatcher.";
            KP_LOG("RenderLog", LOG_LEVEL_ERROR, "%s", last_diagnostic_.c_str());
            return {false, last_diagnostic_};
        }
        try
        {
            RenderBackendFactory factory = info.backend_factory;
            if (!factory)
            {
                factory = [](GraphicsAPIType api_type)
                { return graphics::RenderBackend::CreateGraphicsBackEnd(api_type); };
            }
            backend_ = factory(info.api_type);
            if (!backend_)
            {
                throw std::runtime_error("No graphics backend is available for the requested API.");
            }
            KP_LOG("RenderLog", LOG_LEVEL_INFO, "RenderSystem selected %s graphics backend",
                   GetGraphicsApiName(info.api_type));
            backend_->BindWindowResize(*info.resize_dispatcher);
            backend_->Initialize(info.native_window);
            backend_initialized_ = true;
            window_capture_ = info.window_capture;

            // Imported scenes can contain many mesh sections. Each section
            // consumes per-pass, per-object, and material constants from the
            // frame arena before deferred lighting allocates its frame block.
            // Keep the arena large enough that a valid scene cannot silently
            // skip lighting after exhausting the old 64 KiB budget.
            constexpr size_t kFrameUniformCapacity = 4 * 1024 * 1024;
            frame_contexts_.resize(backend_->GetFramesInFlight());
            for (FrameContext &context : frame_contexts_)
            {
                context.Initialize(*backend_, kFrameUniformCapacity);
            }

            lifecycle_state_ = RenderSystemLifecycleState::PresentationReady;
            last_diagnostic_.clear();
            return {true, {}};
        }
        catch (const std::exception &error)
        {
            last_diagnostic_ = error.what();
        }
        catch (...)
        {
            last_diagnostic_ = "Unknown exception during RenderSystem presentation initialization.";
        }
        KP_LOG("RenderLog", LOG_LEVEL_ERROR, "RenderSystem presentation initialization failed: %s",
               last_diagnostic_.c_str());
        CleanupOwnedState();
        lifecycle_state_ = RenderSystemLifecycleState::Uninitialized;
        return {false, last_diagnostic_};
    }

    RenderSystemInitResult RenderSystem::PromoteToScene(
        std::shared_ptr<const PreparedRenderAssetCatalog> prepared_assets)
    {
        if (lifecycle_state_ != RenderSystemLifecycleState::PresentationReady)
        {
            last_diagnostic_ = "RenderSystem scene promotion requires presentation-ready state.";
            return {false, last_diagnostic_};
        }
        if (!prepared_assets)
        {
            last_diagnostic_ = "RenderSystem scene promotion requires prepared assets.";
            return {false, last_diagnostic_};
        }
        try
        {
            prepared_assets_ = std::move(prepared_assets);
            material_system_ = std::make_unique<MaterialSystem>();
            resource_resolver_ = std::make_unique<RenderResourceResolver>(
                *backend_, *prepared_assets_);
            material_system_->SetResourceResolver(resource_resolver_.get());
            scene_coordinator_.Bind(*material_system_, *resource_resolver_, prepared_assets_);
            KP_LOG("RenderLog", LOG_LEVEL_INFO,
                   "Prepared render catalog contains %u shader(s)",
                   static_cast<unsigned>(prepared_assets_->GetPreparedShaderCount()));

            const graphics::Extent2D extent = backend_->GetRenderExtent();
            deferred_renderer_ = std::make_unique<DeferredRenderer>();
            const DeferredRendererInitResult renderer_result = deferred_renderer_->Initialize(
                {*backend_, *resource_resolver_, *material_system_, *prepared_assets_},
                extent.width, extent.height);
            if (!renderer_result)
            {
                throw std::runtime_error(renderer_result.diagnostic);
            }
            render_capture_service_ = std::make_unique<RenderCaptureService>(
                backend_->GetRenderTargetReadback(),
                [this](CaptureView view)
                {
                    return deferred_renderer_ ? deferred_renderer_->GetCaptureTarget(view)
                                               : graphics::RenderTargetHandle{};
                },
                [this] { return frame_number_; });
            profile_window_.Reset();
            profile_summary_logged_ = false;
            profile_scene_seen_ = false;
            lifecycle_state_ = RenderSystemLifecycleState::Ready;
            last_diagnostic_.clear();
            return {true, {}};
        }
        catch (const std::exception &error)
        {
            last_diagnostic_ = error.what();
        }
        catch (...)
        {
            last_diagnostic_ = "Unknown exception during RenderSystem initialization.";
        }
        KP_LOG("RenderLog", LOG_LEVEL_ERROR, "RenderSystem scene promotion failed: %s",
               last_diagnostic_.c_str());
        CleanupSceneState();
        lifecycle_state_ = RenderSystemLifecycleState::PresentationReady;
        return {false, last_diagnostic_};
    }

    bool RenderSystem::BeginFrame(float delta_time)
    {
        if (lifecycle_state_ != RenderSystemLifecycleState::Ready &&
            lifecycle_state_ != RenderSystemLifecycleState::PresentationReady)
        {
            return false;
        }
        const auto frame_started = std::chrono::steady_clock::now();
        profile_frame_start_ = frame_started;
        const bool scene_ready = deferred_renderer_ != nullptr;
        std::optional<RenderSceneFrameInput> scene_input;
        const auto scene_prepare_started = std::chrono::steady_clock::now();
        if (scene_ready)
        {
            debug_view_ = requested_debug_view_;
            scene_input.emplace(scene_coordinator_.PrepareFrame(
                render_capture_service_ ? render_capture_service_->GetPendingView()
                                        : std::nullopt,
                debug_view_ == CaptureView::SceneColor ? std::nullopt
                                                       : std::optional<CaptureView>{debug_view_}));
            deferred_renderer_->ApplyPendingExtent();
        }
        const double scene_prepare_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - scene_prepare_started)
                .count();
        const auto backend_begin_started = std::chrono::steady_clock::now();
        backend_->BeginFrame();
        const double backend_begin_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - backend_begin_started)
                .count();
        const std::vector<graphics::GpuProfileTiming> completed_gpu_timings =
            backend_->ConsumeCompletedGpuProfileTimings();
        active_frame_context_ = GetCurrentFrameContext();
        if (!active_frame_context_)
        {
            backend_->EndFrame();
            active_frame_context_ = nullptr;
            last_diagnostic_ = "Graphics backend opened a frame without a valid frame context.";
            return false;
        }
        frame_return_state_ = lifecycle_state_;
        lifecycle_state_ = RenderSystemLifecycleState::FrameActive;
        elapsed_seconds_ += delta_time;
        const graphics::Extent2D extent = scene_ready
                                              ? graphics::Extent2D{
                                                    deferred_renderer_->GetSceneRenderTarget().GetWidth(),
                                                    deferred_renderer_->GetSceneRenderTarget().GetHeight()}
                                              : backend_->GetRenderExtent();
        active_frame_context_->Begin(
            backend_->GetCurrentFrameIndex(),
            {frame_number_, elapsed_seconds_, delta_time},
            {extent.width, extent.height});
        if (!scene_ready)
        {
            profile_ = {};
            profile_.frame_number = frame_number_;
            profile_.graphics_api = backend_->GetGraphicsAPI();
            profile_.viewport_width = extent.width;
            profile_.viewport_height = extent.height;
            profile_.cpu_scene_prepare_ms = scene_prepare_ms;
            profile_.cpu_backend_begin_ms = backend_begin_ms;
            profile_.present_mode = backend_->GetPresentModeName();
            return true;
        }
        const auto record_started = std::chrono::steady_clock::now();
        const DeferredRendererFrameResult result =
            deferred_renderer_->RecordFrame(*active_frame_context_, *scene_input);
        const double record_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - record_started)
                .count();
        // The editor's external composite is recorded after BeginFrame has
        // published this snapshot but before EndFrame finalizes it. Preserve
        // the last completed frame's aggregate/finalize/present values so the
        // in-frame profiler does not display artificial zeros during this
        // transition.
        const double previous_cpu_total_ms = profile_.cpu_total_ms;
        const double previous_cpu_finalize_ms = profile_.cpu_finalize_ms;
        const double previous_cpu_present_ms = profile_.cpu_present_ms;
        profile_ = deferred_renderer_->GetProfileSnapshot();
        profile_.summary = profile_window_.GetSummary();
        profile_.cpu_scene_prepare_ms = scene_prepare_ms;
        profile_.cpu_backend_begin_ms = backend_begin_ms;
        profile_.cpu_record_ms = record_ms;
        profile_.cpu_total_ms = previous_cpu_total_ms;
        profile_.cpu_finalize_ms = previous_cpu_finalize_ms;
        profile_.cpu_present_ms = previous_cpu_present_ms;
        profile_.present_mode = backend_->GetPresentModeName();
        for (const graphics::GpuProfileTiming &timing : completed_gpu_timings)
        {
            if (timing.pass_id < profile_.passes.size())
            {
                profile_.passes[timing.pass_id].gpu_time_ms =
                    static_cast<double>(timing.nanoseconds) / 1000000.0;
                profile_.gpu_frame_number = frame_number_;
            }
        }
        if (scene_input->pending_capture.has_value())
        {
            if (!result.capture_target_ready)
            {
                render_capture_service_->RejectPendingCapture(
                    "Render could not record the requested capture-view conversion pass");
            }
            else
            {
                render_capture_service_->EnqueuePendingReadback();
            }
        }
        return true;
    }

    bool RenderSystem::EndFrame()
    {
        if (!IsState(RenderSystemLifecycleState::FrameActive))
        {
            return false;
        }
        const auto finalize_started = std::chrono::steady_clock::now();
        if (active_frame_context_)
        {
            if (deferred_renderer_)
            {
                deferred_renderer_->FinalizeFrame();
            }
            active_frame_context_->End();
            active_frame_context_ = nullptr;
            ++frame_number_;
        }
        profile_.cpu_finalize_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - finalize_started)
                .count();
        const auto present_started = std::chrono::steady_clock::now();
        backend_->EndFrame();
        profile_.cpu_present_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - present_started)
                .count();
        const graphics::BackendProfileCounters backend_profile =
            backend_->GetBackendProfileCounters();
        profile_.descriptor_sets_created = backend_profile.descriptor_sets_created;
        profile_.descriptor_pools_created = backend_profile.descriptor_pools_created;
        profile_.descriptor_searches = backend_profile.descriptor_searches;
        profile_.descriptor_allocations = backend_profile.descriptor_allocations;
        profile_.descriptor_updates = backend_profile.descriptor_updates;
        profile_.descriptor_search_cpu_ms = backend_profile.descriptor_search_cpu_ms;
        profile_.descriptor_allocation_cpu_ms = backend_profile.descriptor_allocation_cpu_ms;
        profile_.descriptor_update_cpu_ms = backend_profile.descriptor_update_cpu_ms;
        profile_.pipeline_validation_cpu_ms =
            backend_profile.recorder.pipeline_validation_cpu_ms;
        profile_.pipeline_validation_calls =
            backend_profile.recorder.pipeline_validation_calls;
        profile_.pipeline_bind_requests = backend_profile.recorder.pipeline_bind_requests;
        profile_.pipeline_bind_emitted = backend_profile.recorder.pipeline_bind_emitted;
        profile_.mesh_bind_requests = backend_profile.recorder.mesh_bind_requests;
        profile_.mesh_bind_emitted = backend_profile.recorder.mesh_bind_emitted;
        profile_.resource_binding_bind_requests =
            backend_profile.recorder.resource_binding_bind_requests;
        profile_.resource_binding_bind_emitted =
            backend_profile.recorder.resource_binding_bind_emitted;
        profile_.native_draw_calls = backend_profile.recorder.draw_calls_emitted;
        profile_.cpu_total_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - profile_frame_start_)
                .count();
        // OpenGL presents outside the backend frame bracket, so defer the
        // sample until RecordPresentationTime() has measured SwapBuffers().
        if (backend_->GetGraphicsAPI() != GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
            ObserveProfileFrame();
            PublishMetricsSnapshot();
        }
        lifecycle_state_ = frame_return_state_;
        return true;
    }

    void RenderSystem::RecordPresentationTime(const double milliseconds)
    {
        if (milliseconds < 0.0)
        {
            return;
        }
        profile_.cpu_present_ms = milliseconds;
        profile_.cpu_total_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - profile_frame_start_)
                .count();
        if (backend_ && backend_->GetGraphicsAPI() == GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
            ObserveProfileFrame();
            PublishMetricsSnapshot();
        }
    }

    void RenderSystem::ObserveProfileFrame()
    {
        // The fixed profile describes the selected Sponza scene. RenderSystem
        // can become scene-ready a few frames before asynchronous level
        // promotion publishes its renderables; do not spend the entire profile
        // window measuring that empty transition.
        if (profile_.draw_calls == 0 || profile_.textures.dependency_count == 0)
        {
            return;
        }
        if (!profile_scene_seen_)
        {
            profile_window_.Reset();
            profile_scene_seen_ = true;
        }
        profile_window_.Observe(profile_);
        profile_.summary = profile_window_.GetSummary();
        LogCompletedProfileSummary();
    }

    void RenderSystem::LogCompletedProfileSummary()
    {
        if (profile_summary_logged_ || !profile_.summary.complete)
        {
            return;
        }
        const RenderProfileScenario scenario = GetSponzaProfileScenario();
        const auto &gbuffer = profile_.summary.passes[
            static_cast<size_t>(RenderProfilePass::GBuffer)];
        const double gbuffer_p95 = gbuffer.gpu_p95_ms.value_or(0.0);
        const auto cpu_subphase_p95 = [this](const RenderProfileCpuSubphase subphase)
        {
            return profile_.summary.cpu_subphases[static_cast<size_t>(subphase)]
                .cpu_p95_ms.value_or(0.0);
        };
        KP_LOG("RenderLog", LOG_LEVEL_DEBUG,
               "Render profile complete: scenario=%s level=%s camera=%s api=%s "
               "viewport=%ux%u warmup=%u samples=%u cpu_p50_ms=%.3f cpu_p95_ms=%.3f "
               "present_p50_ms=%.3f present_p95_ms=%.3f gbuffer_gpu_p95_ms=%.3f "
               "draws=%llu native_draws=%llu sections=%llu descriptor_sets=%llu "
               "descriptor_pools=%llu descriptor_searches=%llu descriptor_allocations=%llu "
               "descriptor_updates=%llu descriptor_ms=[%.3f,%.3f,%.3f] "
               "binds=[pipeline:%llu/%llu mesh:%llu/%llu descriptors:%llu/%llu] "
               "pipeline_validation_ms=%.3f "
               "cpu_subphase_p95_ms=[packets:%.3f shadow:%.3f material:%.3f "
               "uniform:%.3f descriptor_search:%.3f descriptor_alloc:%.3f "
               "descriptor_update:%.3f validation:%.3f] "
               "textures=%u source_bytes=%llu decoded_bytes=%llu resident_bytes=%llu "
               "present_mode=%s shadow_hits=%llu shadow_misses=%llu",
               scenario.name, scenario.startup_level, scenario.camera_id,
               GetGraphicsApiName(profile_.graphics_api), profile_.viewport_width,
               profile_.viewport_height, profile_.summary.warmup_frames_completed,
               profile_.summary.samples_collected, profile_.summary.cpu_total_p50_ms,
               profile_.summary.cpu_total_p95_ms, profile_.summary.cpu_present_p50_ms,
               profile_.summary.cpu_present_p95_ms, gbuffer_p95,
               static_cast<unsigned long long>(profile_.draw_calls),
               static_cast<unsigned long long>(profile_.native_draw_calls),
               static_cast<unsigned long long>(profile_.sections),
               static_cast<unsigned long long>(profile_.descriptor_sets_created),
               static_cast<unsigned long long>(profile_.descriptor_pools_created),
               static_cast<unsigned long long>(profile_.descriptor_searches),
               static_cast<unsigned long long>(profile_.descriptor_allocations),
               static_cast<unsigned long long>(profile_.descriptor_updates),
               profile_.descriptor_search_cpu_ms,
               profile_.descriptor_allocation_cpu_ms,
               profile_.descriptor_update_cpu_ms,
               static_cast<unsigned long long>(profile_.pipeline_bind_requests),
               static_cast<unsigned long long>(profile_.pipeline_bind_emitted),
               static_cast<unsigned long long>(profile_.mesh_bind_requests),
               static_cast<unsigned long long>(profile_.mesh_bind_emitted),
               static_cast<unsigned long long>(profile_.resource_binding_bind_requests),
               static_cast<unsigned long long>(profile_.resource_binding_bind_emitted),
               profile_.pipeline_validation_cpu_ms,
               cpu_subphase_p95(RenderProfileCpuSubphase::SectionPacketBuild),
               cpu_subphase_p95(RenderProfileCpuSubphase::ShadowStampFit),
               cpu_subphase_p95(RenderProfileCpuSubphase::MaterialResolution),
               cpu_subphase_p95(RenderProfileCpuSubphase::UniformWrite),
               cpu_subphase_p95(RenderProfileCpuSubphase::DescriptorSearch),
               cpu_subphase_p95(RenderProfileCpuSubphase::DescriptorAllocation),
               cpu_subphase_p95(RenderProfileCpuSubphase::DescriptorUpdate),
               cpu_subphase_p95(RenderProfileCpuSubphase::PipelineValidation),
               profile_.textures.dependency_count,
               static_cast<unsigned long long>(profile_.textures.source_bytes),
               static_cast<unsigned long long>(profile_.textures.decoded_bytes),
               static_cast<unsigned long long>(profile_.textures.resident_bytes),
               profile_.present_mode.c_str(),
               static_cast<unsigned long long>(profile_.shadow_cache_hits),
               static_cast<unsigned long long>(profile_.shadow_cache_misses));
        profile_summary_logged_ = true;
    }

    bool RenderSystem::CompletePendingWindowCapture()
    {
        if (!render_capture_service_ || !render_capture_service_->HasPendingWindowCapture())
        {
            return false;
        }

        if (!window_capture_)
        {
            render_capture_service_->CompletePendingWindowCapture(
                {CaptureResultStatus::Unavailable, {},
                 "The active window system does not implement window capture"});
            return true;
        }

        try
        {
            render_capture_service_->CompletePendingWindowCapture(window_capture_());
        }
        catch (const std::exception &error)
        {
            render_capture_service_->CompletePendingWindowCapture(
                {CaptureResultStatus::Failed, {},
                 std::string{"Engine window capture threw an exception: "} + error.what()});
        }
        catch (...)
        {
            render_capture_service_->CompletePendingWindowCapture(
                {CaptureResultStatus::Failed, {},
                 "Engine window capture threw an unknown exception"});
        }
        return true;
    }

    bool RenderSystem::ExecuteEditorCompositePass(const std::function<void()> &record_pass)
    {
        if (!IsState(RenderSystemLifecycleState::FrameActive) || !active_frame_context_ ||
            !record_pass || !deferred_renderer_)
        {
            return false;
        }
        const bool succeeded = deferred_renderer_->ExecuteEditorCompositePass(record_pass);
        // The editor composite is recorded after BeginFrame has published the
        // RenderSystem snapshot. Publish its completed CPU scope immediately so
        // the in-frame profiler does not show a stale zero for this pass.
        profile_.passes[static_cast<size_t>(RenderProfilePass::EditorComposite)].cpu_time_ms =
            deferred_renderer_->GetProfileSnapshot()
                .passes[static_cast<size_t>(RenderProfilePass::EditorComposite)]
                .cpu_time_ms;
        return succeeded;
    }

    void RenderSystem::RequestSceneRenderTargetExtent(uint32_t width, uint32_t height)
    {
        if (lifecycle_state_ == RenderSystemLifecycleState::Uninitialized ||
            lifecycle_state_ == RenderSystemLifecycleState::ShutDown || width == 0 || height == 0)
        {
            return;
        }
        if (deferred_renderer_)
        {
            deferred_renderer_->RequestExtent(width, height);
        }
    }

    graphics::RenderTargetView RenderSystem::GetSceneRenderTargetView() const
    {
        return deferred_renderer_ ? deferred_renderer_->GetSceneRenderTarget().GetView()
                                   : graphics::RenderTargetView{};
    }

    std::optional<spatial::Ray> RenderSystem::BuildSceneRay(
        float ndc_x, float ndc_y, float viewport_aspect) const
    {
        if (!deferred_renderer_ || viewport_aspect <= 0.0f)
        {
            return std::nullopt;
        }
        return deferred_renderer_->BuildSceneRay(ndc_x, ndc_y, viewport_aspect);
    }

    std::optional<Vector3f> RenderSystem::ProjectScenePoint(
        const Vector3f &world_point, float viewport_aspect) const
    {
        if (!deferred_renderer_)
        {
            return std::nullopt;
        }
        return deferred_renderer_->ProjectScenePoint(world_point, viewport_aspect);
    }

    void RenderSystem::SetDebugView(CaptureView view)
    {
        if (view == CaptureView::EngineWindow)
        {
            return;
        }
        requested_debug_view_ = view;
    }

    graphics::RenderTargetView RenderSystem::GetDebugRenderTargetView() const
    {
        return deferred_renderer_ ? deferred_renderer_->GetViewportRenderTargetView(debug_view_)
                                   : graphics::RenderTargetView{};
    }

    RenderSystem::RenderSystemMetrics RenderSystem::GetMetrics() const
    {
        RenderSystemMetrics metrics{};
        metrics.prepared_shader_count = prepared_assets_ != nullptr
                                            ? static_cast<uint32_t>(
                                                  prepared_assets_->GetPreparedShaderCount())
                                            : 0U;
        metrics.triangle_count = deferred_renderer_ ? deferred_renderer_->GetTriangleCount() : 0U;
        metrics.gpu_usage_percent = backend_ ? backend_->GetGpuUsagePercent() : std::nullopt;
        metrics.profile = profile_;
        if (lifecycle_state_ == RenderSystemLifecycleState::FrameActive &&
            profile_frame_start_ != std::chrono::steady_clock::time_point{})
        {
            metrics.profile.cpu_total_ms =
                std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - profile_frame_start_)
                    .count();
        }
        return metrics;
    }

    RenderSystem::RenderSystemMetrics RenderSystem::GetPublishedMetrics() const
    {
        const std::shared_ptr<const RenderSystemMetrics> metrics =
            std::atomic_load_explicit(&published_metrics_, std::memory_order_acquire);
        return metrics != nullptr ? *metrics : RenderSystemMetrics{};
    }

    void RenderSystem::PublishMetricsSnapshot()
    {
        auto metrics = std::make_shared<RenderSystemMetrics>();
        metrics->prepared_shader_count = prepared_assets_ != nullptr
                                             ? static_cast<uint32_t>(
                                                   prepared_assets_->GetPreparedShaderCount())
                                             : 0U;
        metrics->triangle_count = deferred_renderer_ ? deferred_renderer_->GetTriangleCount() : 0U;
        metrics->gpu_usage_percent = backend_ ? backend_->GetGpuUsagePercent() : std::nullopt;
        metrics->profile = profile_;
        std::shared_ptr<const RenderSystemMetrics> published = std::move(metrics);
        std::atomic_store_explicit(&published_metrics_, std::move(published),
                                   std::memory_order_release);
    }

    graphics::IEditorPresentationBridge *RenderSystem::GetEditorPresentationBridge()
    {
        return backend_ ? backend_->GetEditorPresentationBridge() : nullptr;
    }

    IRenderCaptureService *RenderSystem::GetRenderCaptureService()
    {
        return render_capture_service_.get();
    }

    FrameContext *RenderSystem::GetCurrentFrameContext()
    {
        if (!backend_ || !backend_->GetCommandRecorder())
        {
            return nullptr;
        }
        const uint32_t index = backend_->GetCurrentFrameIndex();
        return index < frame_contexts_.size() ? &frame_contexts_[index] : nullptr;
    }

    bool RenderSystem::IsState(RenderSystemLifecycleState expected) const
    {
        return lifecycle_state_ == expected;
    }

    void RenderSystem::CleanupOwnedState()
    {
        // If teardown is requested between BeginFrame and EndFrame, close the
        // frame bracket before waiting or destroying any frame-owned resource.
        if (lifecycle_state_ == RenderSystemLifecycleState::FrameActive && backend_)
        {
            if (deferred_renderer_)
            {
                deferred_renderer_->FinalizeFrame();
            }
            if (active_frame_context_)
            {
                active_frame_context_->End();
                active_frame_context_ = nullptr;
            }
            backend_->EndFrame();
        }

        CleanupSceneState();

        for (FrameContext &context : frame_contexts_)
        {
            context.Cleanup();
        }
        frame_contexts_.clear();
        if (deferred_renderer_)
        {
            deferred_renderer_->Cleanup();
            deferred_renderer_.reset();
        }

        if (resource_resolver_)
        {
            resource_resolver_->Cleanup();
            resource_resolver_.reset();
        }
        if (backend_)
        {
            backend_->Cleanup();
            backend_.reset();
        }
        backend_initialized_ = false;
        prepared_assets_.reset();
        active_frame_context_ = nullptr;
        frame_number_ = 0;
        elapsed_seconds_ = 0.0f;
        profile_ = {};
        profile_window_.Reset();
        profile_summary_logged_ = false;
        profile_scene_seen_ = false;
        profile_frame_start_ = {};
        frame_return_state_ = RenderSystemLifecycleState::Uninitialized;
        window_capture_ = {};
        debug_view_ = CaptureView::SceneColor;
        requested_debug_view_ = CaptureView::SceneColor;
    }

    void RenderSystem::CleanupSceneState()
    {
        scene_coordinator_.Clear();
        material_system_.reset();
        if (backend_ && backend_initialized_)
        {
            // Releasing material instances first retires their bindless table
            // slots. WaitIdle then makes all submitted GPU work safe to retire.
            backend_->WaitIdle();
            if (graphics::IRenderTargetReadback *const readback =
                    backend_->GetRenderTargetReadback())
            {
                readback->DrainPendingReadbacks("Render system scene teardown");
            }
        }
        render_capture_service_.reset();
        if (deferred_renderer_)
        {
            deferred_renderer_->Cleanup();
            deferred_renderer_.reset();
        }
        if (resource_resolver_)
        {
            resource_resolver_->Cleanup();
            resource_resolver_.reset();
        }
        prepared_assets_.reset();
        debug_view_ = CaptureView::SceneColor;
        requested_debug_view_ = CaptureView::SceneColor;
    }

    void RenderSystem::Shutdown()
    {
        if (lifecycle_state_ == RenderSystemLifecycleState::ShutDown)
        {
            return;
        }
        CleanupOwnedState();
        lifecycle_state_ = RenderSystemLifecycleState::ShutDown;
    }
}
