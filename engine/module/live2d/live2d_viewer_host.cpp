#include "live2d_viewer_host.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>

#include <imgui.h>

#include "asset/asset_manager.h"
#include "base/color.h"
#include "config/path.h"
#include "editor/log/editor_log_component.h"
#include "editor/settings/editor_settings.h"
#include "editor/ui/editor_ui.h"
#include "engine.h"
#include "graphics/backend/common/render_backend.h"
#include "log/logger.h"
#include "live2d_model_report_command.h"
#include "live2d_settings.h"
#include "module/live2d/render/live2d_renderer.h"
#include "module/live2d/runtime/live2d_model_resource.h"
#include "panel_glyph_product.h"
#include "render/render_capture_service_internal.h"
#include "screenshot/runtime_screenshot_service.h"
#include "runtime_global_context.h"
#include "window/window_system.h"

namespace kpengine::live2d
{
    namespace
    {
        // Upper bound on how long a --capture waits for its --resize to reach
        // the output target before capturing at whatever extent is current.
        constexpr uint32_t kResizeWaitFrameBudget = 240u;

    }

    struct Live2DViewerUiState final
    {
        std::unique_ptr<editor::EditorUI> ui;
        std::unique_ptr<editor::EditorLogComponent> log;
    };

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

            window_ = WindowSystem::CreateWindowSystem(WindowAPIType::WINDOW_API_GLFW);
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

            backend_ = graphics::RenderBackend::CreateGraphicsBackEnd(engine.GetGraphicsAPI());
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
                auto frame = std::make_unique<render::FrameContext>();
                frame->Initialize(*backend_, 4u * 1024u * 1024u);
                frame_contexts_.push_back(std::move(frame));
            }

            if (!system_.Initialize())
            {
                diagnostic = "Live2D Cubism framework initialization failed";
                return false;
            }
            system_initialized_ = true;
            renderer_ = std::make_unique<Live2DRenderer>(system_, model_asset_);
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
                bubble_glyphs_ = panel::ToGlyphSet(std::move(product));
                bubble_panel_.SetText(0u, *engine.GetPanelText());
                bubble_ink_ = bubble_panel_.Rebuild(bubble_glyphs_);

                bubble_ = std::make_unique<BubbleRenderer>();
                // The pipeline's attachment format has to be the one the model's
                // pass draws into, so it is asked for rather than assumed.
                if (!bubble_->Initialize(*backend_, renderer_->GetOutputColorFormat(),
                                         diagnostic))
                {
                    return false;
                }
                if (!bubble_->UploadText(bubble_ink_, diagnostic))
                {
                    return false;
                }

                // Manga is dark on light, the opposite of a lit display: the
                // panel's own defaults would give a glowing bubble with dark
                // text, which is the wrong reading entirely.
                bubble_appearance_.fill_color = {1.0f, 1.0f, 1.0f, 1.0f};
                bubble_appearance_.outline_color = {0.09f, 0.09f, 0.11f, 1.0f};
                bubble_appearance_.dot_color = {0.09f, 0.09f, 0.11f, 1.0f};
                // The paper's cells are visible against the bezel, so every dot
                // reads as an element whether or not it is lit. A flat white
                // field would make this ink on a page rather than a display.
                bubble_appearance_.bezel_color = {0.74f, 0.76f, 0.82f, 1.0f};
                bubble_appearance_.dot_gap = 0.20f;

                // Off to one side and clear of the character, rather than
                // centred at the top where it sits on the head and hides the
                // face -- the one thing the viewer exists to show. Upper left,
                // where a manga bubble sits when it is leading into a panel. A
                // bubble is also smaller than it first looks: it is sized to its
                // text, so a short line wants a short bubble.
                bubble_placement_.center_x = 0.26f;
                bubble_placement_.center_y = 0.16f;
                bubble_placement_.height_fraction = 0.15f;
                bubble_pop_ = 0.0f;
                bubble_enabled_ = true;
                KP_LOG("Live2DViewer", LOG_LEVEL_INFO,
                       "speech bubble enabled from %s", product_path.c_str());
            }

            editor::EditorSettings editor_settings{};
            editor_settings.log_colors = editor::DefaultLogColors();
            try
            {
                editor_settings = editor::ReadEditorSettings(GetSettingsPath());
            }
            catch (const std::exception &error)
            {
                KP_LOG("Live2DViewer", LOG_LEVEL_WARNING,
                       "viewer log settings unavailable (%s), using defaults", error.what());
            }
            viewer_ui_ = std::make_shared<Live2DViewerUiState>();
            viewer_ui_->log = std::make_unique<editor::EditorLogComponent>(
                runtime::global_runtime_context.log_system_.get(), editor_settings.log_colors,
                editor::EditorWindowConfig{0.0f, 0.75f, 1.0f, 0.25f, true});
            viewer_ui_->ui = std::make_unique<editor::EditorUI>();
            editor::EditorUIInitInfo ui_info{};
            ui_info.window = window_->GetNativeHandle();
            ui_info.editor_presentation_bridge = backend_->GetEditorPresentationBridge();
            ui_info.log_system = runtime::global_runtime_context.log_system_.get();
            ui_info.engine = &engine;
            ui_info.background_color_override = editor::LogColor{
                window_background_color[0], window_background_color[1],
                window_background_color[2], window_background_color[3]};
            viewer_ui_->ui->InitializeViewer(ui_info, [this] { RenderViewerUI(); });

            render_capture_service_ = std::make_unique<render::RenderCaptureService>(
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
            screenshot_service_ = std::make_unique<runtime::RuntimeScreenshotService>(
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
                    bubble_enabled_ &&
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
        system_.Tick(delta_time);
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

    bool Live2DViewerHost::AdvanceBubblePop(const float delta_time)
    {
        // Short enough to read as a pop rather than as a transition, and long
        // enough that a capture taken at the first frame would catch it partway.
        constexpr float kPopSeconds = 0.18f;
        if (bubble_pop_ < 1.0f)
        {
            bubble_pop_ += delta_time / kPopSeconds;
            if (bubble_pop_ > 1.0f)
            {
                bubble_pop_ = 1.0f;
            }
        }
        return bubble_pop_ >= 1.0f;
    }

    void Live2DViewerHost::RequestCaptureWhenSettled()
    {
        if (capture_waits_for_resize_ || capture_waits_for_bubble_)
        {
            return;
        }
        RequestStartupCapture();
    }

    void Live2DViewerHost::BuildBubbleDraws(const graphics::Extent2D &extent,
                                            std::vector<render::SubmissionDraw> &out)
    {
        if (!bubble_enabled_ || !bubble_ || !renderer_)
        {
            return;
        }

        // Sized to what the panel actually drew rather than to the text's nominal
        // size, so a bubble around "I" is small and one around a sentence is not.
        const panel::DotBounds bounds = panel::LitBounds(bubble_ink_);
        if (bounds.empty)
        {
            return;
        }

        BubbleLayoutRequest request;
        request.text_width = bounds.right - bounds.left + 1u;
        request.text_height = bounds.bottom - bounds.top + 1u;
        // The tail points at the model, which is the one thing the bubble needs
        // to know about it. It arrives as a direction and not as a Live2D type:
        // the renderer publishes where the model was fitted, in the same
        // normalized space the placement is expressed in.
        const Live2DRenderer::ModelBounds model = renderer_->GetModelBounds();
        if (model.valid)
        {
            const float model_center_x = (model.min_x + model.max_x) * 0.5f;
            const float model_center_y = (model.min_y + model.max_y) * 0.5f;
            request.tail = BubbleTailFromDirection(
                model_center_x - bubble_placement_.center_x,
                model_center_y - bubble_placement_.center_y);
        }
        else
        {
            request.tail = BubbleTail::Down;
        }

        BubbleLayout layout;
        if (!BuildBubbleLayout(request, layout))
        {
            return;
        }

        bubble_placement_.progress = bubble_pop_;
        // Logged once per run: what the bubble's size and place were decided
        // from. A cross-backend difference in the bubble and not the model can
        // only come from one of these, so they are worth having in the record.
        if (!bubble_placement_logged_)
        {
            bubble_placement_logged_ = true;
            KP_LOG("Live2DViewer", LOG_LEVEL_INFO,
                   "bubble placement: progress %.4f target_aspect %.4f center %.3f,%.3f "
                   "height_fraction %.3f quad %.4fx%.4f aspect %.4f text %ux%u dots",
                   bubble_pop_, bubble_placement_.target_aspect,
                   bubble_placement_.center_x, bubble_placement_.center_y,
                   bubble_placement_.height_fraction, layout.quad_width,
                   layout.quad_height, layout.Aspect(), request.text_width,
                   request.text_height);
        }
        // The frame's own extent, which is what the model's target is resized to
        // and therefore what the bubble is drawn into. Reading the output view
        // instead gave a square reading on the first frame, and a wrong aspect
        // does not fail here -- it stretches the bubble by the ratio, which is
        // how it came out a third too wide.
        const std::uint32_t width = extent.width;
        const std::uint32_t height = extent.height;
        if (width == 0u || height == 0u)
        {
            return;
        }
        bubble_placement_.target_aspect =
            static_cast<float>(width) / static_cast<float>(height);

        graphics::Viewport viewport{};
        viewport.width = static_cast<float>(width);
        viewport.height = static_cast<float>(height);

        std::string bubble_diagnostic;
        if (!bubble_->BuildDraws(layout, bubble_placement_, bubble_appearance_, viewport,
                                 out, bubble_diagnostic))
        {
            KP_LOG("Live2DViewer", LOG_LEVEL_WARNING, "bubble draw skipped: %s",
                   bubble_diagnostic.c_str());
        }
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
        if (bubble_enabled_)
        {
            const bool settled = AdvanceBubblePop(kViewerDeltaSeconds);
            if (settled && capture_waits_for_bubble_)
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
        const auto render_started = std::chrono::steady_clock::now();
        // The bubble's draws join the model's pass. A pass of its own would
        // re-apply the target's clear and leave an image containing only the
        // bubble -- which validates, renders, and is the wrong picture.
        std::vector<render::SubmissionDraw> bubble_draws;
        BuildBubbleDraws(extent, bubble_draws);
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
        if (viewer_ui_ && viewer_ui_->ui && !viewer_ui_->ui->Render())
        {
            diagnostic = "Live2D viewer ImGui presentation failed";
            frame.End();
            backend_->EndFrame();
            return false;
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
        runtime::command::CommandRegistrationResult registration =
            RegisterLive2DModelReportCommand(
                registry,
                [this](Live2DModelReport &report)
                {
                    if (renderer_ == nullptr)
                    {
                        return false;
                    }
                    report.model_path = loaded_model_path_;
                    report.features = renderer_->GetFeatureReport();
                    report.capabilities = renderer_->GetBehaviorCapabilities();
                    report.behavior_mask = renderer_->GetLastBehaviorMask();
                    report.update_sequence = renderer_->GetLastFrameSequence();
                    return true;
                });
        if (!registration.IsSuccess())
        {
            diagnostic = registration.diagnostic;
            return false;
        }
        // Holding the token is what keeps the entry installed; the registry
        // releases it when this host is destroyed.
        command_registration_ = std::move(registration.registration);
        return true;
    }

    void Live2DViewerHost::RenderViewerUI()
    {
        ImGuiViewport *const viewport = ImGui::GetMainViewport();
        const float viewer_width = viewport->WorkSize.x * 0.70f;
        const float viewer_height = viewport->WorkSize.y * 0.75f;
        ImGui::SetNextWindowPos(viewport->WorkPos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(viewer_width, viewer_height),
                                 ImGuiCond_Always);
        constexpr ImGuiWindowFlags kViewerFlags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse;
        viewer_image_valid_ = false;
        if (ImGui::Begin("Live2D Viewer", nullptr, kViewerFlags))
        {
            const ImVec2 available = ImGui::GetContentRegionAvail();
            constexpr float kModelAspect = 720.0f / 960.0f;
            const float image_height = std::min(available.y,
                                                available.x / kModelAspect);
            const ImVec2 image_size(image_height * kModelAspect, image_height);
            const ImVec2 cursor = ImGui::GetCursorPos();
            ImGui::SetCursorPos(ImVec2(cursor.x + (available.x - image_size.x) * 0.5f,
                                       cursor.y + (available.y - image_size.y) * 0.5f));
            const ImVec2 image_min = ImGui::GetCursorScreenPos();
            viewer_image_min_ = {image_min.x, image_min.y};
            viewer_image_size_ = {image_size.x, image_size.y};
            viewer_image_valid_ = image_size.x > 0.0f && image_size.y > 0.0f;
            viewer_ui_->ui->DrawRenderTarget(renderer_->GetOutputView(), image_size);
        }
        ImGui::End();

        if (viewer_ui_ && viewer_ui_->log)
        {
            viewer_ui_->log->Render();
        }
        RenderControlPanel();
        RenderProfilerWindow();
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
        Live2DVector2 target{};
        if (gaze_mode_ == GazeMode::FixedTarget)
        {
            target.x = std::clamp(fixed_gaze_target_.x, -1.0f, 1.0f);
            target.y = std::clamp(fixed_gaze_target_.y, -1.0f, 1.0f);
        }
        else if (gaze_mode_ == GazeMode::FollowMouse && cursor_position_valid_ &&
                 viewer_image_valid_ && viewer_image_size_.x > 0.0f &&
                 viewer_image_size_.y > 0.0f)
        {
            const float normalized_x =
                (cursor_position_.x - viewer_image_min_.x) / viewer_image_size_.x;
            const float normalized_y =
                (cursor_position_.y - viewer_image_min_.y) / viewer_image_size_.y;
            target.x = std::clamp(normalized_x * 2.0f - 1.0f, -1.0f, 1.0f);
            target.y = std::clamp(1.0f - normalized_y * 2.0f, -1.0f, 1.0f);
        }
        input.gaze_target = target;
        last_gaze_target_ = target;
        return input;
    }

    void Live2DViewerHost::RenderControlPanel()
    {
        ImGuiViewport *const viewport = ImGui::GetMainViewport();
        const ImVec2 panel_pos(viewport->WorkPos.x + viewport->WorkSize.x * 0.70f,
                               viewport->WorkPos.y);
        const ImVec2 panel_size(viewport->WorkSize.x * 0.30f,
                                viewport->WorkSize.y * 0.43f);
        ImGui::SetNextWindowPos(panel_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(panel_size, ImGuiCond_Always);
        constexpr ImGuiWindowFlags kPanelFlags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

        if (ImGui::Begin("Live2D Control Deck", nullptr, kPanelFlags))
        {
            ImGui::TextDisabled("RUNTIME DEBUG");
            ImGui::Separator();
            RenderDebugControls();
        }
        ImGui::End();

    }

    void Live2DViewerHost::RenderDebugControls()
    {
        static constexpr const char *kGazeLabels[] = {
            "Neutral", "Follow Mouse", "Fixed Target"};
        static constexpr const char *kBehaviorLabels[] = {
            "Blink", "Gaze", "Breath", "Physics", "Pose"};
        static constexpr std::uint32_t kBehaviorBits[] = {
            kLive2DBehaviorBlink, kLive2DBehaviorGaze, kLive2DBehaviorBreath,
            kLive2DBehaviorPhysics, kLive2DBehaviorPose};
        constexpr std::size_t kBehaviorCount = sizeof(kBehaviorLabels) / sizeof(kBehaviorLabels[0]);

        if (ImGui::BeginTabBar("##live2d_control_tabs"))
        {
            if (ImGui::BeginTabItem("Gaze"))
            {
                ImGui::Text("GAZE TARGETING");
                ImGui::TextDisabled("The runtime smooths this target before applying it.");
                int gaze_mode = static_cast<int>(gaze_mode_);
                gaze_mode = std::clamp(gaze_mode, 0, 2);
                if (ImGui::BeginCombo("Mode", kGazeLabels[gaze_mode]))
                {
                    for (int index = 0; index < 3; ++index)
                    {
                        const bool selected = gaze_mode == index;
                        if (ImGui::Selectable(kGazeLabels[index], selected))
                        {
                            gaze_mode_ = static_cast<GazeMode>(index);
                        }
                        if (selected)
                        {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                if (gaze_mode_ == GazeMode::FixedTarget)
                {
                    float target[2] = {fixed_gaze_target_.x, fixed_gaze_target_.y};
                    if (ImGui::SliderFloat2("Fixed target", target, -1.0f, 1.0f))
                    {
                        fixed_gaze_target_ = {target[0], target[1]};
                    }
                }
                ImGui::Separator();
                ImGui::TextDisabled("CURRENT TARGET");
                ImGui::Text("(%+.2f, %+.2f)", last_gaze_target_.x, last_gaze_target_.y);
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Playback"))
            {
                ImGui::Text("PLAYBACK CONTROL");
                ImGui::TextDisabled("Freeze authored motion while inspecting a pose.");
                if (paused_)
                {
                    if (ImGui::Button("RESUME", ImVec2(-1.0f, 0.0f)))
                    {
                        paused_ = false;
                        step_requested_ = false;
                    }
                }
                else if (ImGui::Button("PAUSE", ImVec2(-1.0f, 0.0f)))
                {
                    paused_ = true;
                }
                if (ImGui::Button("STEP ONE FRAME", ImVec2(-1.0f, 0.0f)))
                {
                    paused_ = true;
                    step_requested_ = true;
                }
                if (ImGui::Button("RESET PARAMETERS", ImVec2(-1.0f, 0.0f)))
                {
                    reset_parameters_requested_ = true;
                }
                ImGui::Separator();
                ImGui::Text("State: %s", paused_ ? "PAUSED" : "PLAYING");
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Telemetry"))
            {
                ImGui::Text("RUNTIME TELEMETRY");
                if (renderer_ == nullptr)
                {
                    ImGui::TextDisabled("Live2D runtime is not ready");
                }
                else
                {
                    const Live2DBehaviorCapabilities capabilities =
                        renderer_->GetBehaviorCapabilities();
                    const Live2DRenderFeatureReport &features = renderer_->GetFeatureReport();
                    const std::uint32_t behavior_mask = renderer_->GetLastBehaviorMask();
                    ImGui::Text("Update sequence  %llu",
                                static_cast<unsigned long long>(renderer_->GetLastUpdateSequence()));
                    ImGui::Text("Snapshot sequence %llu",
                                static_cast<unsigned long long>(renderer_->GetLastFrameSequence()));
                    ImGui::Text("Parameters  %llu   Drawables  %u",
                                static_cast<unsigned long long>(renderer_->GetParameterCount()),
                                features.drawable_count);
                    ImGui::Separator();
                    ImGui::TextDisabled("ACTIVE BEHAVIORS");
                    for (std::size_t index = 0; index < kBehaviorCount; ++index)
                    {
                        const bool active = (behavior_mask & kBehaviorBits[index]) != 0u;
                        ImGui::Text(active ? "[ ON ]  %s" : "[ -- ]  %s",
                                   kBehaviorLabels[index]);
                        if ((index % 2u) == 0u && index + 1u < kBehaviorCount)
                        {
                            ImGui::SameLine(150.0f);
                        }
                    }
                    ImGui::Separator();
                    ImGui::Text("Hit areas  %s", capabilities.has_hit_areas ? "available" : "none");
                    ImGui::Text("User data   %s", capabilities.has_user_data ? "available" : "none");
                    ImGui::Text("Secondary   %s",
                                capabilities.has_secondary_behavior ? "available" : "reimport required");
                    ImGui::Text("Behavior mask  0x%08X", static_cast<unsigned>(behavior_mask));
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    void Live2DViewerHost::RenderProfilerWindow()
    {
        ImGuiViewport *const viewport = ImGui::GetMainViewport();
        const ImVec2 profiler_pos(viewport->WorkPos.x + viewport->WorkSize.x * 0.70f,
                                  viewport->WorkPos.y + viewport->WorkSize.y * 0.44f);
        const ImVec2 profiler_size(viewport->WorkSize.x * 0.30f,
                                   viewport->WorkSize.y * 0.31f);
        ImGui::SetNextWindowPos(profiler_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(profiler_size, ImGuiCond_Always);
        constexpr ImGuiWindowFlags kProfilerFlags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoSavedSettings;

        if (ImGui::Begin("Performance Profiler", nullptr, kProfilerFlags))
        {
            ImGui::TextDisabled("GPU submission and frame timing");
            ImGui::Separator();
            const graphics::BackendProfileCounters counters =
                backend_ != nullptr ? backend_->GetBackendProfileCounters()
                                    : graphics::BackendProfileCounters{};
            const graphics::CommandRecorderProfileCounters recorder =
                backend_ != nullptr && backend_->GetCommandRecorder() != nullptr
                    ? backend_->GetCommandRecorder()->GetProfileCounters()
                    : counters.recorder;
            ImGui::Text("API: %s", engine_ != nullptr &&
                                       engine_->GetGraphicsAPI() ==
                                           GraphicsAPIType::GRAPHICS_API_VULKAN
                                   ? "Vulkan"
                                   : "OpenGL");
            ImGui::Separator();
            ImGui::Text("Frame %.2f ms", frame_total_ms_);
            ImGui::Text("Render work %.2f ms", render_work_ms_);
            ImGui::Text("ImGui work %.2f ms", imgui_work_ms_);
            ImGui::Text("Tick work %.2f ms", game_tick_work_ms_);
            ImGui::Separator();
            ImGui::TextDisabled("Live2D / backend");
            ImGui::Text("Draw calls %llu",
                        static_cast<unsigned long long>(
                            recorder.draw_calls_emitted));
            ImGui::Text("Pipeline binds %llu",
                        static_cast<unsigned long long>(
                            recorder.pipeline_bind_emitted));
            ImGui::Text("Resource binds %llu",
                        static_cast<unsigned long long>(
                            recorder.resource_binding_bind_emitted));
            ImGui::Text("Descriptor updates %llu",
                        static_cast<unsigned long long>(counters.descriptor_updates));
            ImGui::Text("Module GPU handles %u",
                        renderer_ != nullptr ? renderer_->GetLiveGpuHandleCount() : 0u);
            ImGui::Text("Frame sequence %llu",
                        static_cast<unsigned long long>(
                            renderer_ != nullptr ? renderer_->GetLastFrameSequence() : 0u));
            ImGui::Separator();
            ImGui::TextDisabled("ImGui");
            ImGui::Text("Build %.2f ms", viewer_ui_->ui->GetLastImGuiBuildTimeMs());
            ImGui::Text("Submit %.2f ms", viewer_ui_->ui->GetLastImGuiSubmitTimeMs());
            ImGui::Text("Total %.2f ms", viewer_ui_->ui->GetLastRenderTimeMs());
        }
        ImGui::End();

    }

    void Live2DViewerHost::CleanupGpu() noexcept
    {
        if (viewer_ui_)
        {
            if (viewer_ui_->ui)
            {
                viewer_ui_->ui->Close();
            }
            viewer_ui_.reset();
        }
        if (backend_ && backend_initialized_)
        {
            backend_->WaitIdle();
        }
        screenshot_service_.reset();
        render_capture_service_.reset();
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
        for (const std::unique_ptr<render::FrameContext> &frame : frame_contexts_)
        {
            if (frame)
            {
                frame->Cleanup();
            }
        }
        frame_contexts_.clear();
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
        if (system_initialized_)
        {
            system_.Shutdown();
            system_initialized_ = false;
        }
        model_asset_ = {};
        engine_ = nullptr;
    }
}
