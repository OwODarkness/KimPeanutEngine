#include "live2d_viewer_host.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <exception>
#include <utility>

#include "asset/asset_manager.h"
#include "base/color.h"
#include "config/path.h"
#include "editor/settings/editor_settings.h"
#include "editor/live2d_viewer_bubble.h"
#include "editor/live2d_viewer_command_bridge.h"
#include "editor/live2d_viewer_editor.h"
#include "engine.h"
#include "graphics/backend/common/render_backend.h"
#include "log/logger.h"
#include "live2d_model_report_command.h"
#include "live2d_settings.h"
#include "module/live2d/emotion/live2d_emotion_runtime.h"
#include "module/live2d/render/live2d_renderer.h"
#include "module/live2d/runtime/live2d_model_resource.h"
#include "panel_glyph_product.h"
#include "render/frame_context.h"
#include "render/render_capture_service_internal.h"
#include "runtime/live2d_system.h"
#include "screenshot/runtime_screenshot_service.h"
#include "runtime_global_context.h"
#include "window/window_system.h"

namespace kpengine::live2d
{
    template <typename T>
    void Live2DViewerHostDeleter<T>::operator()(T *const object) const noexcept
    {
        delete object;
    }

    template <typename T, typename... Args>
    Live2DViewerHostPtr<T> MakeHostObject(Args &&...args)
    {
        return Live2DViewerHostPtr<T>(
            new T(std::forward<Args>(args)...));
    }

    template struct Live2DViewerHostDeleter<WindowSystem>;
    template struct Live2DViewerHostDeleter<graphics::RenderBackend>;
    template struct Live2DViewerHostDeleter<render::FrameContext>;
    template struct Live2DViewerHostDeleter<Live2DSystem>;
    template struct Live2DViewerHostDeleter<Live2DRenderer>;
    template struct Live2DViewerHostDeleter<editor::Live2DViewerBubble>;
    template struct Live2DViewerHostDeleter<editor::Live2DViewerEditor>;
    template struct Live2DViewerHostDeleter<editor::Live2DViewerCommandBridge>;
    template struct Live2DViewerHostDeleter<Live2DEmotionRuntime>;
    template struct Live2DViewerHostDeleter<render::RenderCaptureService>;
    template struct Live2DViewerHostDeleter<runtime::RuntimeScreenshotService>;

    namespace
    {
        // Upper bound on how long a --capture waits for its --resize to reach
        // the output target before capturing at whatever extent is current.
        constexpr uint32_t kResizeWaitFrameBudget = 240u;
        const char *TerminalStateName(
            const Live2DBehaviorTerminalState state) noexcept
        {
            switch (state)
            {
            case Live2DBehaviorTerminalState::Pending:
                return "pending";
            case Live2DBehaviorTerminalState::Completed:
                return "completed";
            case Live2DBehaviorTerminalState::Cancelled:
                return "cancelled";
            case Live2DBehaviorTerminalState::Interrupted:
                return "interrupted";
            default:
                return "unknown";
            }
        }

        const char *TransitionReasonName(
            const Live2DBehaviorTransitionReason reason) noexcept
        {
            switch (reason)
            {
            case Live2DBehaviorTransitionReason::Initial:
                return "initial";
            case Live2DBehaviorTransitionReason::IntentChanged:
                return "intent_changed";
            case Live2DBehaviorTransitionReason::Fallback:
                return "fallback";
            case Live2DBehaviorTransitionReason::Retriggered:
                return "retriggered";
            case Live2DBehaviorTransitionReason::LowerPriorityRejected:
                return "lower_priority_rejected";
            case Live2DBehaviorTransitionReason::SameStateIgnored:
                return "same_state_ignored";
            default:
                return "unknown";
            }
        }

    }

    Live2DViewerHost::~Live2DViewerHost()
    {
        Shutdown();
    }

    bool Live2DViewerHost::Initialize(runtime::Engine &engine, std::string &diagnostic)
    {
        diagnostic.clear();
        if (engine.GetApplicationMode() != runtime::ApplicationMode::Live2DViewer)
        {
            diagnostic = "Live2D viewer host can only initialize in live2d-viewer mode";
            return false;
        }
        if (runtime::global_runtime_context.AreSceneServicesInitialized())
        {
            diagnostic =
                "Live2D viewer cannot initialize while Scene3D services are present";
            return false;
        }

        try
        {
            engine_ = &engine;
            const Live2DSettings settings = ReadLive2DSettings(
                ComposePath(project_root, "config/live2d.json"));
            if (!settings.enabled)
            {
                diagnostic = "Live2D viewer is disabled by config/live2d.json";
                return false;
            }

            // --live2d-model selects a locally provisioned product for this run;
            // the tracked config keeps naming the fixture that is always present.
            const std::string &preview_asset =
                engine.GetLive2DModelOverride().has_value()
                    ? *engine.GetLive2DModelOverride()
                    : settings.preview_asset;
            const std::string model_path = GetAssetDirectory() + preview_asset;
            loaded_model_path_ = preview_asset;
            model_asset_ = asset::AssetManager::GetInstance().LoadSync(model_path);
            if (!model_asset_.IsValid() || model_asset_.type != kLive2DModelAssetType)
            {
                diagnostic = "Configured Live2D product could not be loaded: " + model_path;
                return false;
            }

            window_ = Live2DViewerHostPtr<WindowSystem>(
                WindowSystem::CreateWindowSystem(WindowAPIType::WINDOW_API_GLFW).release());
            if (!window_)
            {
                diagnostic = "Live2D viewer could not create a window system";
                return false;
            }
            WindowCreateInfo window_info{};
            window_info.width = 720;
            window_info.height = 960;
            window_info.title = "KimPeanut Live2D Viewer";
            window_info.graphics_api_type = engine.GetGraphicsAPI();
            if (!window_->Initialize(window_info))
            {
                diagnostic = "Live2D viewer window initialization failed";
                return false;
            }
            window_initialized_ = true;
            window_->cursor_event_dispatcher_.Bind([this](const CursorEvent &event)
            {
                HandleCursorEvent(event);
            });

            backend_ = Live2DViewerHostPtr<graphics::RenderBackend>(
                graphics::RenderBackend::CreateGraphicsBackEnd(engine.GetGraphicsAPI()).release());
            if (!backend_)
            {
                diagnostic = "Live2D viewer could not create its graphics backend";
                return false;
            }
            backend_->BindWindowResize(window_->resize_event_dispatcher_);
            backend_->Initialize(window_->GetNativeHandle());
            backend_initialized_ = true;

            const uint32_t frame_count = std::max(1u, backend_->GetFramesInFlight());
            frame_contexts_.reserve(frame_count);
            for (uint32_t index = 0; index < frame_count; ++index)
            {
                auto frame = MakeHostObject<render::FrameContext>();
                frame->Initialize(*backend_, 4u * 1024u * 1024u);
                frame_contexts_.push_back(std::move(frame));
            }

            system_ = MakeHostObject<Live2DSystem>();
            if (!system_->Initialize())
            {
                diagnostic = "Live2D Cubism framework initialization failed";
                return false;
            }
            system_initialized_ = true;
            renderer_ = MakeHostObject<Live2DRenderer>(*system_, model_asset_);
            renderer_->SetPresentationTarget(false);
            const std::array<float, 4> window_background_color =
                ReadWindowBackgroundColor(GetSettingsPath());
            renderer_->SetBackgroundColor(window_background_color);
            // The configured color is display-space; the output target is sRGB
            // on both backends, so the clear must be linear there. Hardware
            // re-encodes on store, leaving the configured value on screen.
            std::array<float, 4> output_clear_color = window_background_color;
            for (std::size_t channel = 0u; channel < 3u; ++channel)
            {
                output_clear_color[channel] =
                    SrgbToLinear(output_clear_color[channel]);
            }
            if (engine.GetStartupCaptureTransparentClear())
            {
                // Validation runs need the product's own alpha and blend
                // coverage to survive into the exported image instead of being
                // composited onto an opaque backdrop.
                output_clear_color[3] = 0.0f;
            }
            renderer_->SetOutputClearColor(output_clear_color);
            if (!renderer_->Initialize(*backend_, window_info.width, window_info.height,
                                       diagnostic))
            {
                return false;
            }
            render_initialized_ = true;

            const auto model_resource =
                asset::AssetManager::GetInstance().GetResource<Live2DModelResource>(
                    model_asset_);
            if (model_resource == nullptr)
            {
                emotion_runtime_ = MakeHostObject<Live2DEmotionRuntime>();
                std::string replay_diagnostic =
                    "Live2D viewer behavior replay could not read the loaded product";
                KP_LOG("Live2DViewer", LOG_LEVEL_WARNING, "%s",
                       replay_diagnostic.c_str());
            }
            else
            {
                emotion_runtime_ = MakeHostObject<Live2DEmotionRuntime>();
                std::string replay_diagnostic;
                if (!emotion_runtime_->RunReplay(model_resource->Product(), replay_diagnostic))
                {
                    const Live2DEmotionReplayResult &replay = emotion_runtime_->Replay();
                    KP_LOG("Live2DViewer", LOG_LEVEL_WARNING,
                           "Live2D emotion replay unavailable: %s",
                           replay.diagnostic.c_str());
                }
                else
                {
                    const Live2DEmotionReplayResult &replay = emotion_runtime_->Replay();
                    KP_LOG("Live2DViewer", LOG_LEVEL_INFO,
                           "Live2D emotion replay passed: %llu transitions, deterministic=%d",
                           static_cast<unsigned long long>(
                               replay.transition_history.size()),
                           replay.deterministic ? 1 : 0);
                }
            }

            // Viewer policy starts the configured product's authored Idle clip. Other products
            // can remain static until a viewer command selects a valid motion.
            std::string preview_motion_diagnostic;
            if (!renderer_->StartPreviewMotion("Idle", 0u, 1,
                                               preview_motion_diagnostic))
            {
                KP_LOG("Live2DViewer", LOG_LEVEL_WARNING,
                       "Live2D preview motion unavailable; keeping static pose (%s)",
                       preview_motion_diagnostic.c_str());
            }

            // The speech bubble, when both of its inputs were given. Without them
            // the viewer is exactly what it was, which is what keeps this from
            // changing every existing capture.
            if (engine.GetPanelText().has_value() &&
                engine.GetPanelGlyphProduct().has_value())
            {
                const std::string product_path =
                    GetAssetDirectory() + *engine.GetPanelGlyphProduct();
                panel::PanelGlyphProduct product;
                try
                {
                    product = panel::ReadPanelGlyphProduct(product_path);
                }
                catch (const std::exception &error)
                {
                    diagnostic = std::string("bubble glyph product could not be read: ") +
                                 error.what();
                    return false;
                }
                bubble_ = MakeHostObject<editor::Live2DViewerBubble>();
                // The pipeline's attachment format has to be the one the model's
                // pass draws into, so it is asked for rather than assumed.
                if (!bubble_->Initialize(*backend_, renderer_->GetOutputColorFormat(),
                                         diagnostic) ||
                    !bubble_->SetGlyphs(panel::ToGlyphSet(std::move(product)), diagnostic) ||
                    !bubble_->SetInitialText(*engine.GetPanelText(), diagnostic))
                {
                    return false;
                }
                KP_LOG("Live2DViewer", LOG_LEVEL_INFO,
                       "speech bubble enabled from %s", product_path.c_str());
            }

            kpengine::editor::EditorSettings editor_settings{};
            editor_settings.log_colors = kpengine::editor::DefaultLogColors();
            try
            {
                editor_settings = kpengine::editor::ReadEditorSettings(GetSettingsPath());
            }
            catch (const std::exception &error)
            {
                KP_LOG("Live2DViewer", LOG_LEVEL_WARNING,
                       "viewer log settings unavailable (%s), using defaults", error.what());
            }
            viewer_editor_ = MakeHostObject<editor::Live2DViewerEditor>();
            kpengine::editor::EditorUIInitInfo ui_info{};
            ui_info.window = window_->GetNativeHandle();
            ui_info.editor_presentation_bridge = backend_->GetEditorPresentationBridge();
            ui_info.log_system = runtime::global_runtime_context.log_system_.get();
            ui_info.engine = &engine;
            ui_info.background_color_override = kpengine::editor::LogColor{
                window_background_color[0], window_background_color[1],
                window_background_color[2], window_background_color[3]};
            if (!viewer_editor_->Initialize(ui_info, editor_settings.log_colors, diagnostic))
            {
                return false;
            }

            render_capture_service_ = MakeHostObject<render::RenderCaptureService>(
                backend_->GetRenderTargetReadback(),
                [this](render::CaptureView view)
                {
                    if (view == render::CaptureView::HostOutput && renderer_)
                    {
                        return renderer_->GetOutputTarget();
                    }
                    return graphics::RenderTargetHandle{};
                },
                [this] { return frame_number_; });
            screenshot_service_ = MakeHostObject<runtime::RuntimeScreenshotService>(
                *render_capture_service_);

            if (engine.GetStartupResize().has_value())
            {
                // Defer the capture: it must read the post-resize target, so the
                // exported image's dimensions are themselves the evidence that
                // the resize executed.
                pending_resize_ = engine.GetStartupResize();
            }
            else
            {
                // A bubble has to be given the chance to finish popping in before
                // the image is taken, or the capture records a moment in the
                // middle of an animation nobody asked to see.
                capture_waits_for_bubble_ =
                    bubble_ != nullptr && bubble_->IsEnabled() &&
                    engine.GetStartupCaptureOverride().has_value();
                RequestCaptureWhenSettled();
            }
            return true;
        }
        catch (const std::exception &error)
        {
            diagnostic = error.what();
            return false;
        }
        catch (...)
        {
            diagnostic = "unknown Live2D viewer initialization failure";
            return false;
        }
    }

    bool Live2DViewerHost::Tick(const float delta_time, std::string &diagnostic)
    {
        diagnostic.clear();
        if (!system_initialized_)
        {
            diagnostic = "Live2D viewer system is not initialized";
            return false;
        }
        const auto tick_started = std::chrono::steady_clock::now();
        system_->Tick(delta_time);
        game_tick_work_ms_ = std::chrono::duration<double, std::milli>(
                                 std::chrono::steady_clock::now() - tick_started)
                                 .count();
        return true;
    }

    void Live2DViewerHost::ApplyPendingResize()
    {
        if (!pending_resize_.has_value() || window_ == nullptr)
        {
            return;
        }

        // One shot: a validation run resizes exactly once, so the frame the
        // startup capture reads is downstream of a single extent transition.
        const runtime::RuntimeResizeRequest resize = *pending_resize_;
        pending_resize_.reset();
        applied_resize_ = resize;
        // A backend may apply a window resize lazily -- Vulkan defers it to a
        // frame boundary -- so requesting the capture here would export the
        // pre-resize image. Wait until the output target reports the requested
        // extent instead.
        capture_waits_for_resize_ =
            engine_ != nullptr && engine_->GetStartupCaptureOverride().has_value();

        // A real window resize, not just a recorded extent: a Vulkan backend
        // recreates its surface from what the platform granted, so a size the
        // window system only cached would leave the swapchain at the old extent.
        window_->RequestWindowSize(static_cast<int>(resize.width),
                                   static_cast<int>(resize.height));

        if (backend_ != nullptr)
        {
            const graphics::Extent2D extent = backend_->GetRenderExtent();
            KP_LOG("Live2DViewer", LOG_LEVEL_INFO,
                   "Live2D viewer resize to %ux%u requested; backend reports %ux%u",
                   resize.width, resize.height, extent.width, extent.height);
        }
    }

    void Live2DViewerHost::RequestStartupCapture()
    {
        if (engine_ == nullptr || screenshot_service_ == nullptr ||
            !engine_->GetStartupCaptureOverride().has_value())
        {
            return;
        }

        exit_after_capture_ = engine_->GetStartupExitAfterCapture();
        runtime::ScreenshotRequest request{};
        // The standalone viewer presents directly to the swapchain, so the
        // default capture reads the presentation boundary. The product view
        // reads the viewer's own output target instead, which is what makes two
        // backends comparable: it carries no host UI.
        request.capture.view =
            engine_->GetStartupCaptureView() == runtime::StartupCaptureView::Product
                ? render::CaptureView::HostOutput
                : render::CaptureView::EngineWindow;
        request.output_path = *engine_->GetStartupCaptureOverride();
        screenshot_service_->RequestScreenshot(
            std::move(request),
            [this](runtime::ScreenshotResult result)
            {
                if (result.IsSuccess())
                {
                    // The capture is only interpretable next to the revision it
                    // came from, so publish both together.
                    const Live2DRenderCounters &counters = renderer_->GetLastCounters();
                    KP_LOG("Live2DViewer", LOG_LEVEL_INFO,
                           "Startup capture exported to %s (frame_sequence %llu, behavior_mask %u, draws %u, "
                           "mask_sources %u, mask_contexts %u, position_upload_bytes %llu, typed_playback %d, secondary_behavior %d)",
                           result.output_path.c_str(),
                           static_cast<unsigned long long>(renderer_->GetLastFrameSequence()),
                           renderer_->GetLastBehaviorMask(),
                           counters.submitted_draw_count,
                           counters.submitted_mask_source_draw_count,
                           counters.active_mask_context_count,
                           static_cast<unsigned long long>(counters.position_upload_bytes),
                           renderer_->GetBehaviorCapabilities().has_typed_playback ? 1 : 0,
                           renderer_->GetBehaviorCapabilities().has_secondary_behavior ? 1 : 0);
                }
                else
                {
                    KP_LOG("Live2DViewer", LOG_LEVEL_ERROR,
                           "Startup capture failed: %s", result.diagnostic.c_str());
                }
                capture_settled_ = true;
            });
    }

    void Live2DViewerHost::RequestCaptureWhenSettled()
    {
        if (capture_waits_for_resize_ || capture_waits_for_bubble_)
        {
            return;
        }
        RequestStartupCapture();
    }

    bool Live2DViewerHost::RecordFrame(std::string &diagnostic)
    {
        diagnostic.clear();
        if (!render_initialized_ || !window_ || !backend_ || frame_contexts_.empty() ||
            !renderer_)
        {
            diagnostic = "Live2D viewer render state is not initialized";
            return false;
        }
        const auto frame_started = std::chrono::steady_clock::now();
        window_->PollEvents();
        if (window_->ShouldClose())
        {
            return true;
        }

        // A validation run records its first frame at the authored extent and
        // then resizes once, so the renderer's output-resize path executes
        // against a genuinely different target instead of only being re-entered
        // at an unchanged extent. Both the dispatch and the deferred capture
        // request run outside the frame bracket.
        if (pending_resize_.has_value() && frame_number_ > 0u)
        {
            ApplyPendingResize();
        }
        if (capture_waits_for_resize_)
        {
            // Gated on the renderer's own output view, not on the backend's
            // reported extent: the view is what the capture actually reads.
            const graphics::RenderTargetView output = renderer_->GetOutputView();
            const bool at_requested_extent =
                output.IsValid() && output.width == applied_resize_.width &&
                output.height == applied_resize_.height;
            ++resize_wait_frames_;
            if (at_requested_extent ||
                resize_wait_frames_ > kResizeWaitFrameBudget)
            {
                if (!at_requested_extent)
                {
                    KP_LOG("Live2DViewer", LOG_LEVEL_WARNING,
                           "Live2D viewer resize to %ux%u never reached the output "
                           "target within %u frames; capturing at the current extent",
                           applied_resize_.width, applied_resize_.height,
                           resize_wait_frames_);
                }
                capture_waits_for_resize_ = false;
                RequestCaptureWhenSettled();
            }
        }

        backend_->BeginFrame();
        const uint32_t frame_index = backend_->GetCurrentFrameIndex() %
                                     static_cast<uint32_t>(frame_contexts_.size());
        render::FrameContext &frame = *frame_contexts_[frame_index];
        const graphics::Extent2D extent = backend_->GetRenderExtent();
        if (extent.width == 0u || extent.height == 0u)
        {
            backend_->EndFrame();
            return true;
        }
        // The resize runs outside the frame's active bracket, before any Live2D
        // work is recorded, so the old target is only released after the
        // renderer's own idle wait.
        if (!renderer_->ResizeOutput(extent.width, extent.height, diagnostic))
        {
            if (!renderer_->GetOutputView().IsValid())
            {
                // Nothing valid to render into, so this frame cannot proceed.
                backend_->EndFrame();
                return false;
            }
            if (!output_resize_failed_)
            {
                output_resize_failed_ = true;
                KP_LOG("Live2DViewer", LOG_LEVEL_WARNING,
                       "Live2D output resize failed; keeping the last valid target (%s)",
                       diagnostic.c_str());
            }
            diagnostic.clear();
        }
        else
        {
            output_resize_failed_ = false;
        }
        graphics::CommandRecorder *const recorder = backend_->GetCommandRecorder();
        if (recorder == nullptr)
        {
            backend_->EndFrame();
            return true;
        }
        constexpr float kViewerDeltaSeconds = 1.0f / 120.0f;
        // The pop-in is advanced by the frame, not by the shape, so the bubble's
        // animation is a property of the viewer's clock rather than of its
        // geometry.
        if (bubble_ && bubble_->IsEnabled())
        {
            bubble_->Tick(kViewerDeltaSeconds);
            if (bubble_->IsPopSettled() && capture_waits_for_bubble_)
            {
                capture_waits_for_bubble_ = false;
                RequestCaptureWhenSettled();
            }
        }
        const bool advance_frame = !paused_ || step_requested_;
        const Live2DFrameInput frame_input = BuildFrameInput(kViewerDeltaSeconds);
        const bool reset_parameters = reset_parameters_requested_;
        step_requested_ = false;
        reset_parameters_requested_ = false;
        if (advance_frame)
        {
            elapsed_seconds_ += kViewerDeltaSeconds;
        }
        frame.Begin(frame_index, {frame_number_++, elapsed_seconds_, kViewerDeltaSeconds}, extent);
        if (bubble_)
        {
            // FrameContext::Begin releases this slot's descriptor sets after
            // BeginFrame has waited for their submission. Retire masks only
            // after that release, so no descriptor can still reference them.
            bubble_->CollectRetiredResources();
        }
        const auto render_started = std::chrono::steady_clock::now();
        // The bubble's draws join the model's pass. A pass of its own would
        // re-apply the target's clear and leave an image containing only the
        // bubble -- which validates, renders, and is the wrong picture.
        std::vector<render::SubmissionDraw> bubble_draws;
        if (bubble_ && renderer_)
        {
            const Live2DRenderer::ModelBounds model = renderer_->GetModelBounds();
            editor::Live2DViewerBubbleModelBounds bounds{};
            bounds.valid = model.valid;
            bounds.min_x = model.min_x;
            bounds.min_y = model.min_y;
            bounds.max_x = model.max_x;
            bounds.max_y = model.max_y;
            bubble_->BuildDraws(extent, bounds, bubble_draws);
        }
        if (!renderer_->Record(frame, *recorder, kViewerDeltaSeconds, frame_input,
                               advance_frame, reset_parameters, bubble_draws, diagnostic))
        {
            if (render_capture_service_ && render_capture_service_->HasPendingCapture())
            {
                render_capture_service_->RejectPendingCapture(
                    "Live2D renderer failed before capture readback was queued");
            }
            frame.End();
            backend_->EndFrame();
            return false;
        }
        render_work_ms_ = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - render_started)
                              .count();
        if (render_capture_service_)
        {
            render_capture_service_->EnqueuePendingReadback();
        }
        // Publish the current frame's CPU work before ImGui draws the profiler;
        // the final value below includes presentation completion for the next
        // frame's display as well.
        frame_total_ms_ = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - frame_started)
                              .count();
        const auto imgui_started = std::chrono::steady_clock::now();
        if (viewer_editor_)
        {
            editor::Live2DViewerEditorActions actions;
            actions.set_gaze = [this](const editor::Live2DGazeMode mode,
                                      const Vector2f target) {
                gaze_mode_ = mode;
                fixed_gaze_target_ = target;
            };
            actions.set_paused = [this](const bool paused) {
                paused_ = paused;
                if (!paused_)
                {
                    step_requested_ = false;
                }
            };
            actions.request_step = [this] {
                paused_ = true;
                step_requested_ = true;
            };
            actions.request_reset_parameters = [this] {
                reset_parameters_requested_ = true;
            };
            actions.apply_emotion = [this](const std::string_view preset) {
                ApplyEmotionPreset(preset);
            };
            actions.report_image_rect = [this](const Vector2f minimum,
                                               const Vector2f size) {
                viewer_image_min_ = minimum;
                viewer_image_size_ = size;
                viewer_image_valid_ = size[0] > 0.0f && size[1] > 0.0f;
            };
            if (!viewer_editor_->Render(BuildEditorState(), std::move(actions), diagnostic))
            {
                frame.End();
                backend_->EndFrame();
                return false;
            }
        }
        imgui_work_ms_ = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - imgui_started)
                              .count();
        frame.End();
        backend_->EndFrame();
        if (engine_->GetGraphicsAPI() == GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
            CompleteWindowCapture();
            window_->SwapBuffers();
        }
        else
        {
            CompleteWindowCapture();
        }
        frame_total_ms_ = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - frame_started)
                              .count();
        return true;
    }

    void Live2DViewerHost::CompleteWindowCapture() noexcept
    {
        if (!render_capture_service_ || !render_capture_service_->HasPendingWindowCapture())
        {
            return;
        }

        try
        {
            WindowCaptureResult capture = window_->CaptureWindow();
            render::CaptureResult result{};
            if (!capture.IsSuccess())
            {
                result.status = render::CaptureResultStatus::Unavailable;
                result.diagnostic = std::move(capture.diagnostic);
            }
            else
            {
                result.status = render::CaptureResultStatus::Captured;
                result.image.width = capture.width;
                result.image.height = capture.height;
                result.image.rgba8_pixels = std::move(capture.rgba8_pixels);
            }
            render_capture_service_->CompletePendingWindowCapture(std::move(result));
        }
        catch (const std::exception &error)
        {
            render_capture_service_->CompletePendingWindowCapture(
                {render::CaptureResultStatus::Failed, {},
                 std::string{"Live2D viewer window capture threw an exception: "} +
                     error.what()});
        }
        catch (...)
        {
            render_capture_service_->CompletePendingWindowCapture(
                {render::CaptureResultStatus::Failed, {},
                 "Live2D viewer window capture threw an unknown exception"});
        }
    }

    bool Live2DViewerHost::ShouldClose() const noexcept
    {
        // A resolved capture is a completed validation run, so the host may
        // stop on its own. A failed capture also stops: the run has nothing
        // left to produce, and exiting is what makes the shutdown path -- and
        // its leaked-handle accounting -- observable.
        if (exit_after_capture_ && capture_settled_)
        {
            return true;
        }
        return window_ != nullptr && window_->ShouldClose();
    }

    WindowSystem *Live2DViewerHost::GetHostWindow() noexcept
    {
        return window_.get();
    }

    bool Live2DViewerHost::RegisterHostCommands(
        runtime::command::CommandRegistry &registry, std::string &diagnostic)
    {
        editor::Live2DViewerCommandSources sources;
        sources.model_report = [this](Live2DModelReport &report) {
            if (renderer_ == nullptr)
            {
                return false;
            }
            report.model_path = loaded_model_path_;
            report.features = renderer_->GetFeatureReport();
            report.capabilities = renderer_->GetBehaviorCapabilities();
            report.behavior_mask = renderer_->GetLastBehaviorMask();
            report.update_sequence = renderer_->GetLastFrameSequence();
            const Live2DEmotionReplayResult &replay = emotion_runtime_->Replay();
            report.behavior_replay_available = replay.available;
            report.behavior_replay_passed = replay.passed;
            report.behavior_replay_deterministic = replay.deterministic;
            report.behavior_state = replay.final_snapshot.state_id;
            report.behavior_transition_sequence =
                replay.final_snapshot.transition_sequence;
            report.behavior_history_count = replay.transition_history.size();
            report.behavior_replay_diagnostic = replay.diagnostic;
            return true;
        };
        sources.screenshot_service = [this] { return screenshot_service_.get(); };
        if (!command_bridge_)
        {
            command_bridge_ = MakeHostObject<editor::Live2DViewerCommandBridge>();
        }
        return command_bridge_->Register(registry, std::move(sources), diagnostic);
    }

    void Live2DViewerHost::HandleCursorEvent(const CursorEvent &event) noexcept
    {
        if (!std::isfinite(event.xpos) || !std::isfinite(event.ypos))
        {
            cursor_position_valid_ = false;
            return;
        }
        cursor_position_ = {static_cast<float>(event.xpos),
                            static_cast<float>(event.ypos)};
        cursor_position_valid_ = true;
    }

    Live2DFrameInput Live2DViewerHost::BuildFrameInput(const float delta_time)
    {
        Live2DFrameInput input{};
        input.delta_seconds = delta_time;
        Vector2f target{};
        if (gaze_mode_ == editor::Live2DGazeMode::FixedTarget)
        {
            target[0] = std::clamp(fixed_gaze_target_[0], -1.0f, 1.0f);
            target[1] = std::clamp(fixed_gaze_target_[1], -1.0f, 1.0f);
        }
        else if (gaze_mode_ == editor::Live2DGazeMode::FollowMouse &&
                 cursor_position_valid_ && viewer_image_valid_ &&
                 viewer_image_size_[0] > 0.0f && viewer_image_size_[1] > 0.0f)
        {
            const float normalized_x =
                (cursor_position_[0] - viewer_image_min_[0]) / viewer_image_size_[0];
            const float normalized_y =
                (cursor_position_[1] - viewer_image_min_[1]) / viewer_image_size_[1];
            target[0] = std::clamp(normalized_x * 2.0f - 1.0f, -1.0f, 1.0f);
            target[1] = std::clamp(1.0f - normalized_y * 2.0f, -1.0f, 1.0f);
        }
        input.gaze_target = target;
        last_gaze_target_ = target;
        return input;
    }

    editor::Live2DViewerEditorState Live2DViewerHost::BuildEditorState() const
    {
        editor::Live2DViewerEditorState state;
        state.output_view = renderer_ != nullptr ? renderer_->GetOutputView()
                                                 : graphics::RenderTargetView{};
        state.graphics_api = engine_ != nullptr ? engine_->GetGraphicsAPI()
                                                : GraphicsAPIType::GRAPHICS_API_OPENGL;
        state.gaze_mode = gaze_mode_;
        state.fixed_gaze_target = fixed_gaze_target_;
        state.last_gaze_target = last_gaze_target_;
        state.paused = paused_;
        if (emotion_runtime_)
        {
            state.emotion_status = emotion_runtime_->Status();
            state.emotion_diagnostic = emotion_runtime_->Diagnostic();
        }
        state.frame_total_ms = frame_total_ms_;
        state.render_work_ms = render_work_ms_;
        state.imgui_work_ms = imgui_work_ms_;
        state.game_tick_work_ms = game_tick_work_ms_;
        state.renderer_ready = renderer_ != nullptr;
        if (renderer_ != nullptr)
        {
            state.capabilities = renderer_->GetBehaviorCapabilities();
            state.features = renderer_->GetFeatureReport();
            state.behavior_mask = renderer_->GetLastBehaviorMask();
            state.update_sequence = renderer_->GetLastUpdateSequence();
            state.frame_sequence = renderer_->GetLastFrameSequence();
            state.parameter_count = renderer_->GetParameterCount();
            state.live_gpu_handles = renderer_->GetLiveGpuHandleCount();
        }
        if (backend_ != nullptr)
        {
            state.backend_counters = backend_->GetBackendProfileCounters();
            state.recorder_counters = state.backend_counters.recorder;
            if (backend_->GetCommandRecorder() != nullptr)
            {
                state.recorder_counters = backend_->GetCommandRecorder()->GetProfileCounters();
            }
        }
        return state;
    }

    void Live2DViewerHost::CleanupGpu() noexcept
    {
        if (viewer_editor_)
        {
            viewer_editor_->Close();
            viewer_editor_.reset();
        }
        if (backend_ && backend_initialized_)
        {
            backend_->WaitIdle();
        }
        if (command_bridge_)
        {
            command_bridge_->Reset();
            command_bridge_.reset();
        }
        screenshot_service_.reset();
        render_capture_service_.reset();
        for (const Live2DViewerHostPtr<render::FrameContext> &frame : frame_contexts_)
        {
            if (frame)
            {
                // Release descriptor sets before destroying the textures they
                // reference; Vulkan validates that relationship at image-view teardown.
                frame->Cleanup();
            }
        }
        frame_contexts_.clear();
        if (renderer_)
        {
            // Release every module GPU handle explicitly, in reverse creation
            // order, and record the residue so a leak is visible instead of
            // silently surviving to process exit.
            renderer_->Cleanup();
            shutdown_leaked_handles_ = renderer_->GetLiveGpuHandleCount();
            if (shutdown_leaked_handles_ != 0u)
            {
                try
                {
                    KP_LOG("Live2DViewer", LOG_LEVEL_WARNING,
                           "Live2D shutdown released with %u module GPU handles still live",
                           shutdown_leaked_handles_);
                }
                catch (...)
                {
                }
            }
        }
        renderer_.reset();
        if (bubble_)
        {
            // Released here and not by the destructor, because its handles belong
            // to the backend this function is about to destroy. A bubble left to
            // tear down later calls WaitIdle on a dangling backend, which is a
            // segfault at exit rather than a leak report.
            bubble_->Cleanup();
            shutdown_leaked_handles_ += bubble_->GetLiveGpuHandleCount();
            bubble_.reset();
        }
        if (backend_)
        {
            if (backend_initialized_)
            {
                backend_->Cleanup();
            }
            backend_.reset();
        }
        if (window_)
        {
            if (window_initialized_)
            {
                window_->Cleanup();
            }
            window_.reset();
        }
        render_initialized_ = false;
        backend_initialized_ = false;
        window_initialized_ = false;
    }

    void Live2DViewerHost::ShutdownRenderThread() noexcept
    {
        CleanupGpu();
    }

    void Live2DViewerHost::Shutdown() noexcept
    {
        CleanupGpu();
        if (system_initialized_ && system_)
        {
            system_->Shutdown();
            system_.reset();
            system_initialized_ = false;
        }
        model_asset_ = {};
        engine_ = nullptr;
    }

    bool Live2DViewerHost::ShowEmotionBubble(const std::string_view text,
                                             std::string &diagnostic)
    {
        if (backend_ == nullptr || renderer_ == nullptr)
        {
            diagnostic = "Emotion bubble needs an initialized viewer";
            return false;
        }
        if (!bubble_)
        {
            bubble_ = MakeHostObject<editor::Live2DViewerBubble>();
            if (!bubble_->Initialize(*backend_, renderer_->GetOutputColorFormat(), diagnostic))
            {
                bubble_.reset();
                return false;
            }
        }
        return bubble_->ShowEmotion(text, diagnostic);
    }

    void Live2DViewerHost::ApplyEmotionPreset(const std::string_view preset)
    {
        if (renderer_ == nullptr)
        {
            return;
        }

        Live2DEmotionActions actions;
        actions.clear_expression = [this](std::string &diagnostic) {
            renderer_->ClearPreviewExpression(diagnostic);
        };
        actions.set_expression = [this](const std::string_view expression,
                                         std::string &diagnostic) {
            return renderer_->SetPreviewExpression(expression, diagnostic);
        };
        actions.start_motion = [this](const Live2DEmotionMotionCandidate &candidate,
                                      std::string &diagnostic) {
            return renderer_->StartPreviewMotion(candidate.group, candidate.index, 2,
                                                  diagnostic);
        };
        actions.show_bubble = [this](const std::string_view text,
                                     std::string &diagnostic) {
            return ShowEmotionBubble(text, diagnostic);
        };
        if (emotion_runtime_)
        {
            emotion_runtime_->Apply(preset, actions);
        }
    }
}
