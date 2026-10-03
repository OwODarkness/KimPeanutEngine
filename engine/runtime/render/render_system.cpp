#include "render_system.h"

#include <algorithm>
#include <atomic>
#include <cmath>
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

    bool RenderSystem::SupportsCompleteBlockCompressedTextureProfile() const noexcept
    {
        if (!backend_)
        {
            return false;
        }
        const graphics::GraphicsCapabilities &capabilities = backend_->GetCapabilities();
        return capabilities.SupportsTextureFormat(TextureFormat::TEXTURE_FORMAT_BC4_UNORM) &&
               capabilities.SupportsTextureFormat(TextureFormat::TEXTURE_FORMAT_BC5_UNORM) &&
               capabilities.SupportsTextureFormat(TextureFormat::TEXTURE_FORMAT_BC3_UNORM) &&
               capabilities.SupportsTextureFormat(TextureFormat::TEXTURE_FORMAT_BC3_SRGB);
    }

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
            ray_tracing_enabled_ = info.ray_tracing_enabled;
            path_tracing_enabled_ = info.ray_tracing_enabled && info.path_tracing_enabled;
            requested_path_trace_settings_.path_tracing_enabled = path_tracing_enabled_;
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
            std::array<uint32_t, static_cast<size_t>(RenderProfilePass::Count)> profile_pass_ids{};
            for (size_t index = 0; index < profile_pass_ids.size(); ++index)
            {
                profile_pass_ids[index] = GetRenderGpuProfilePassId(
                    static_cast<RenderProfilePass>(index));
            }
            if (!backend_->ConfigureGpuProfilePasses(profile_pass_ids))
            {
                throw std::runtime_error("Graphics rejected the Render GPU profile pass configuration.");
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
                {*backend_, *resource_resolver_, *material_system_, *prepared_assets_,
                 ray_tracing_enabled_, path_tracing_enabled_},
                extent.width, extent.height);
            if (!renderer_result)
            {
                throw std::runtime_error(renderer_result.diagnostic);
            }
            PathTraceSettings initial_path_trace_settings;
            {
                std::lock_guard lock(request_mutex_);
                initial_path_trace_settings = requested_path_trace_settings_;
            }
            deferred_renderer_->SetPathTraceSettings(initial_path_trace_settings);
            render_capture_service_ = std::make_unique<RenderCaptureService>(
                backend_->GetRenderTargetReadback(),
                [this](CaptureView view)
                {
                    return deferred_renderer_ ? deferred_renderer_->GetCaptureTarget(view)
                                               : graphics::RenderTargetHandle{};
                },
                [this] { return frame_number_; });
            profile_window_.Reset();
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

    uint64_t RenderSystem::QueuePreparedAssetsUpdate(
        std::shared_ptr<const PreparedRenderAssetCatalog> prepared_assets)
    {
        if (!prepared_assets) return 0;
        std::lock_guard lock(request_mutex_);
        if (pending_catalog_update_)
        {
            catalog_update_results_[pending_catalog_update_->first] = {
                PreparedAssetsUpdateStatus::Superseded,
                "A newer prepared catalog update replaced this pending update."};
        }
        const uint64_t serial = next_catalog_update_++;
        pending_catalog_update_ = std::make_pair(serial, std::move(prepared_assets));
        catalog_update_results_[serial] = {PreparedAssetsUpdateStatus::Pending, {}};
        while (catalog_update_results_.size() > 128)
        {
            const auto completed = std::find_if(catalog_update_results_.begin(),
                catalog_update_results_.end(), [](const auto &entry) {
                    return entry.second.status != PreparedAssetsUpdateStatus::Pending;
                });
            if (completed == catalog_update_results_.end()) break;
            catalog_update_results_.erase(completed);
        }
        return serial;
    }

    void RenderSystem::QueueEditorPresentationAssets(
        std::shared_ptr<const PreparedRenderAssetCatalog> prepared_assets)
    {
        std::lock_guard lock(request_mutex_);
        pending_editor_presentation_assets_ = std::move(prepared_assets);
    }

    PreparedAssetsUpdateResult RenderSystem::GetPreparedAssetsUpdateResult(
        const uint64_t serial) const
    {
        std::lock_guard lock(request_mutex_);
        const auto result = catalog_update_results_.find(serial);
        return result != catalog_update_results_.end()
            ? result->second : PreparedAssetsUpdateResult{};
    }

    bool RenderSystem::BeginFrame(float delta_time)
    {
        if (lifecycle_state_ != RenderSystemLifecycleState::Ready &&
            lifecycle_state_ != RenderSystemLifecycleState::PresentationReady)
        {
            return false;
        }
        std::optional<std::pair<uint64_t,
            std::shared_ptr<const PreparedRenderAssetCatalog>>> catalog_update;
        {
            std::lock_guard lock(request_mutex_);
            catalog_update = std::exchange(pending_catalog_update_, std::nullopt);
            if (pending_editor_presentation_assets_ != nullptr)
            {
                editor_presentation_assets_ = std::move(
                    pending_editor_presentation_assets_);
            }
        }
        if (catalog_update)
        {
            const auto previous_catalog = prepared_assets_;
            if (lifecycle_state_ == RenderSystemLifecycleState::Ready)
            {
                CleanupSceneState();
                lifecycle_state_ = RenderSystemLifecycleState::PresentationReady;
            }
            const RenderSystemInitResult promoted = PromoteToScene(catalog_update->second);
            if (!promoted)
            {
                const std::string replacement_diagnostic = promoted.diagnostic;
                if (previous_catalog)
                {
                    const RenderSystemInitResult restored = PromoteToScene(previous_catalog);
                    if (!restored)
                    {
                        last_diagnostic_ = replacement_diagnostic +
                            "; previous render catalog could not be restored: " + restored.diagnostic;
                    }
                }
                else
                {
                    last_diagnostic_ = replacement_diagnostic;
                }
                {
                    std::lock_guard lock(request_mutex_);
                    catalog_update_results_[catalog_update->first] = {
                        PreparedAssetsUpdateStatus::Failed, last_diagnostic_};
                }
                return false;
            }
            applied_catalog_update_.store(catalog_update->first, std::memory_order_release);
            {
                std::lock_guard lock(request_mutex_);
                catalog_update_results_[catalog_update->first] = {
                    PreparedAssetsUpdateStatus::Applied, {}};
            }
        }
        const auto frame_started = std::chrono::steady_clock::now();
        profile_frame_start_ = frame_started;
        const bool scene_ready = deferred_renderer_ != nullptr;
        std::optional<RenderSceneFrameInput> scene_input;
        const auto scene_prepare_started = std::chrono::steady_clock::now();
        if (scene_ready)
        {
            std::optional<CaptureView> editor_debug_view;
            std::optional<CaptureView> tooling_debug_view;
            std::optional<PathTraceSettings> path_trace_settings;
            std::optional<ScreenSpaceAoSettings> screen_space_ao_settings;
            {
                std::lock_guard lock(request_mutex_);
                editor_debug_view = debug_view_demands_[static_cast<std::size_t>(
                    DebugViewConsumer::EditorDebugViewer)];
                tooling_debug_view = debug_view_demands_[static_cast<std::size_t>(
                    DebugViewConsumer::RuntimeTooling)];
                path_trace_settings = std::exchange(pending_path_trace_settings_, std::nullopt);
                screen_space_ao_settings = std::exchange(
                    pending_screen_space_ao_settings_, std::nullopt);
            }
            debug_view_ = editor_debug_view.value_or(
                tooling_debug_view.value_or(CaptureView::SceneColor));
            if (path_trace_settings.has_value())
            {
                deferred_renderer_->SetPathTraceSettings(*path_trace_settings);
            }
            if (screen_space_ao_settings.has_value())
            {
                deferred_renderer_->SetScreenSpaceAoSettings(*screen_space_ao_settings);
            }
            if (requested_profile_window_reset_.exchange(false, std::memory_order_acq_rel))
            {
                profile_window_.Reset();
            }
            if (requested_path_trace_dispatch_failure_.exchange(
                    false, std::memory_order_acq_rel))
            {
                deferred_renderer_->InjectNextPathTraceDispatchFailure();
            }
            const std::optional<CaptureView> capture_view =
                render_capture_service_ ? render_capture_service_->GetPendingView()
                                         : std::nullopt;
            scene_input.emplace(scene_coordinator_.PrepareFrame(
                capture_view,
                debug_view_ == CaptureView::SceneColor
                    ? std::nullopt
                    : std::optional<CaptureView>{debug_view_}));
            deferred_renderer_->ApplyPendingExtent();
        }
        const double scene_prepare_ms =
            std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - scene_prepare_started)
                .count();
        const auto backend_begin_started = std::chrono::steady_clock::now();
        backend_->BeginFrame();
        if (scene_ready && resource_resolver_->TickRetiredTextures())
        {
            // A retired texture is about to lose its image view. Descriptor sets
            // built from it must not outlive it -- Vulkan forbids destroying a
            // view while any set still references it, even one never submitted --
            // so the cached bindings go first and are rebuilt on next use. This
            // runs after the in-flight fence wait above, so no submission still
            // references them either.
            for (FrameContext &frame_context : frame_contexts_)
            {
                frame_context.InvalidateTextureBindings();
            }
            if (deferred_renderer_)
            {
                deferred_renderer_->InvalidateRayTracingTextureBindings();
            }
            resource_resolver_->DestroyRetiredTextures();
        }
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
            const size_t pass_index = GetRenderProfilePassIndex(timing.pass_id);
            if (pass_index < profile_.passes.size())
            {
                profile_.passes[pass_index].gpu_time_ms =
                    static_cast<double>(timing.nanoseconds) / 1000000.0;
                profile_.gpu_frame_number = frame_number_;
            }
        }
        profile_.gpu_timing_samples = static_cast<uint32_t>(completed_gpu_timings.size());
        if (scene_input->pending_capture.has_value())
        {
            if (!result.capture_target_ready || !result.normal_recording_completed)
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
        bool frame_finalized = true;
        if (active_frame_context_)
        {
            if (deferred_renderer_)
            {
                frame_finalized = deferred_renderer_->FinalizeFrame();
                profile_.graph_pass_outcomes =
                    deferred_renderer_->GetProfileSnapshot().graph_pass_outcomes;
                if (!frame_finalized)
                {
                    last_diagnostic_ =
                        "Render graph execution failed while finalizing the frame.";
                    KP_LOG("RenderLog", LOG_LEVEL_ERROR, "%s", last_diagnostic_.c_str());
                }
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
        profile_.cpu_fence_wait_ms = backend_profile.cpu_fence_wait_ms;
        profile_.cpu_acquire_wait_ms = backend_profile.cpu_acquire_wait_ms;
        profile_.cpu_queue_present_ms = backend_profile.cpu_queue_present_ms;
        profile_.ray_tracing_descriptor_sets_created =
            backend_profile.ray_tracing.descriptor_sets_created;
        profile_.ray_tracing_descriptor_pools_created =
            backend_profile.ray_tracing.descriptor_pools_created;
        profile_.ray_tracing_address_table_buffers_created =
            backend_profile.ray_tracing.address_table_buffers_created;
        profile_.ray_tracing_address_table_upload_bytes =
            backend_profile.ray_tracing.address_table_upload_bytes;
        profile_.ray_tracing_acceleration_structure_storage_bytes =
            backend_profile.ray_tracing.acceleration_structure_storage_bytes;
        profile_.ray_tracing_blas_builds = backend_profile.ray_tracing.blas_builds;
        profile_.ray_tracing_blas_updates = backend_profile.ray_tracing.blas_updates;
        profile_.ray_tracing_tlas_builds = backend_profile.ray_tracing.tlas_builds;
        profile_.ray_tracing_tlas_updates = backend_profile.ray_tracing.tlas_updates;
        profile_.ray_tracing_retired_acceleration_structures =
            backend_profile.ray_tracing.retired_acceleration_structures;
        profile_.ray_tracing_retired_descriptor_sets =
            backend_profile.ray_tracing.retired_descriptor_sets;
        profile_.ray_tracing_retired_temporary_buffer_batches =
            backend_profile.ray_tracing.retired_temporary_buffer_batches;
        profile_.ray_tracing_build_diagnostics =
            backend_profile.ray_tracing.recent_builds;
        profile_.ray_tracing_build_diagnostic_count =
            backend_profile.ray_tracing.recent_build_count;
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
        return frame_finalized;
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
        SetDebugViewDemand(DebugViewConsumer::RuntimeTooling, view);
    }

    void RenderSystem::SetDebugViewDemand(DebugViewConsumer consumer,
                                         std::optional<CaptureView> view)
    {
        const std::size_t index = static_cast<std::size_t>(consumer);
        if (index >= debug_view_demands_.size() ||
            (view.has_value() && *view == CaptureView::EngineWindow))
        {
            return;
        }
        std::lock_guard lock(request_mutex_);
        debug_view_demands_[index] = view;
    }

    bool RenderSystem::RequestPathTraceSettings(PathTraceSettings settings)
    {
        if (!IsValidPathTraceSettings(settings))
        {
            return false;
        }
        std::lock_guard lock(request_mutex_);
        requested_path_trace_settings_ = settings;
        pending_path_trace_settings_ = settings;
        requested_profile_window_reset_.store(true, std::memory_order_release);
        return true;
    }

    bool RenderSystem::RequestScreenSpaceAoSettings(ScreenSpaceAoSettings settings)
    {
        if (!std::isfinite(settings.radius) || !std::isfinite(settings.bias) ||
            !std::isfinite(settings.strength) || settings.radius <= 0.0f ||
            settings.bias < 0.0f || settings.strength < 0.0f)
            return false;
        settings.radius = std::clamp(settings.radius, 0.05f, 2.0f);
        settings.bias = std::clamp(settings.bias, 0.0f, settings.radius * 0.5f);
        settings.strength = std::clamp(settings.strength, 0.0f, 4.0f);
        std::lock_guard lock(request_mutex_);
        pending_screen_space_ao_settings_ = settings;
        requested_profile_window_reset_.store(true, std::memory_order_release);
        return true;
    }

    void RenderSystem::RequestPathTraceProbeMode(PathTraceProbeMode mode)
    {
        {
            std::lock_guard lock(request_mutex_);
            requested_path_trace_settings_ = ApplyLegacyPathTraceProbeMode(
                requested_path_trace_settings_, mode);
            pending_path_trace_settings_ = requested_path_trace_settings_;
        }
        requested_profile_window_reset_.store(true, std::memory_order_release);
    }

    void RenderSystem::RequestPathTraceDispatchFailureInjection() noexcept
    {
        requested_path_trace_dispatch_failure_.store(true, std::memory_order_release);
    }

    graphics::RenderTargetView RenderSystem::GetDebugRenderTargetView() const
    {
        return deferred_renderer_ ? deferred_renderer_->GetViewportRenderTargetView(debug_view_)
                                   : graphics::RenderTargetView{};
    }

    CaptureView RenderSystem::GetDebugView() const
    {
        return debug_view_;
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
            published_metrics_.load(std::memory_order_acquire);
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
        published_metrics_.store(std::move(published), std::memory_order_release);
    }

    graphics::IEditorPresentationBridge *RenderSystem::GetEditorPresentationBridge()
    {
        return backend_ ? backend_->GetEditorPresentationBridge() : nullptr;
    }

    std::shared_ptr<const PreparedRenderAssetCatalog>
    RenderSystem::GetEditorPresentationAssets() const
    {
        std::lock_guard lock(request_mutex_);
        return editor_presentation_assets_ != nullptr
                   ? editor_presentation_assets_ : pending_editor_presentation_assets_;
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
        profile_scene_seen_ = false;
        profile_frame_start_ = {};
        frame_return_state_ = RenderSystemLifecycleState::Uninitialized;
        window_capture_ = {};
        debug_view_ = CaptureView::SceneColor;
        {
            std::lock_guard lock(request_mutex_);
            debug_view_demands_.fill(std::nullopt);
            pending_path_trace_settings_.reset();
            pending_screen_space_ao_settings_.reset();
            editor_presentation_assets_.reset();
            pending_editor_presentation_assets_.reset();
        }
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
        {
            std::lock_guard lock(request_mutex_);
            debug_view_demands_.fill(std::nullopt);
            pending_path_trace_settings_.reset();
        }
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
