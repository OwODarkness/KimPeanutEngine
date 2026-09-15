#include "panel_viewer_host.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <exception>
#include <utility>

#include "config/path.h"
#include "dot_matrix.h"
#include "engine.h"
#include "glyph_cell.h"
#include "graphics/backend/common/render_backend.h"
#include "log/logger.h"
#include "panel_glyph_product.h"
#include "panel_renderer.h"
#include "render/render_capture_service_internal.h"
#include "runtime_global_context.h"
#include "utf8.h"

namespace kpengine::panel
{
    namespace
    {
        // Two screen pixels per dot: enough that a dot reads as a block rather
        // than a line, which is the whole point of the display.
        constexpr std::uint32_t kViewerWidth = 1024u;
        constexpr std::uint32_t kViewerHeight = 512u;
        constexpr std::uint32_t kUniformCapacity = 4u * 1024u * 1024u;
        // Screen pixels per panel dot. This is the panel's own resolution, not
        // the window's: a dot needs several pixels before its bezel is anything
        // but a slightly dimmer pixel, and the bezel is what makes the panel read
        // as a matrix of elements rather than a low-resolution image.
        constexpr std::uint32_t kPixelsPerDot = 8u;

        std::uint32_t CountLitDots(const DotMatrix &matrix)
        {
            std::uint32_t lit = 0u;
            for (std::uint32_t row = 0u; row < matrix.Height(); ++row)
            {
                for (std::uint32_t column = 0u; column < matrix.Width(); ++column)
                {
                    if (matrix.TestDot(column, row))
                    {
                        ++lit;
                    }
                }
            }
            return lit;
        }
    }

    PanelViewerHost::~PanelViewerHost()
    {
        Shutdown();
    }

    bool PanelViewerHost::Initialize(runtime::Engine &engine, std::string &diagnostic)
    {
        diagnostic.clear();
        if (engine.GetApplicationMode() != runtime::ApplicationMode::PanelViewer)
        {
            diagnostic = "panel viewer host can only initialize in panel-viewer mode";
            return false;
        }
        if (runtime::global_runtime_context.AreSceneServicesInitialized())
        {
            diagnostic = "panel viewer cannot initialize while Scene3D services are present";
            return false;
        }

        try
        {
            engine_ = &engine;

            // The glyph product is the panel's only content dependency. It is
            // Asset-root-relative, so a product derived from a licensed face
            // stays in the git-ignored asset tree.
            if (!engine.GetPanelGlyphProduct().has_value())
            {
                diagnostic = "panel viewer requires --panel-glyph-product";
                return false;
            }
            loaded_glyph_product_ = *engine.GetPanelGlyphProduct();
            const std::string product_path = GetAssetDirectory() + loaded_glyph_product_;
            PanelGlyphProduct product;
            try
            {
                product = ReadPanelGlyphProduct(product_path);
            }
            catch (const std::exception &error)
            {
                diagnostic =
                    std::string("panel glyph product could not be read: ") + error.what();
                return false;
            }
            glyphs_ = ToGlyphSet(std::move(product));
            glyphs_loaded_ = true;

            if (engine.GetPanelText().has_value())
            {
                panel_.SetText(0u, *engine.GetPanelText());
            }
            if (engine.GetPanelDotColor().has_value())
            {
                // Parsed with the same function the command uses, so the launch
                // option and panel.set_appearance accept exactly the spellings
                // panel.report produces.
                std::array<float, 4> color{};
                if (!ParsePanelColor(*engine.GetPanelDotColor(), color))
                {
                    diagnostic = "panel dot colour must be a \"#RRGGBB\" literal";
                    return false;
                }
                appearance_.dot_color = color;
            }
            if (engine.GetPanelAccentColor().has_value())
            {
                std::array<float, 4> accent{};
                if (!ParsePanelColor(*engine.GetPanelAccentColor(), accent))
                {
                    diagnostic = "panel accent colour must be a \"#RRGGBB\" literal";
                    return false;
                }
                appearance_.accent_color = accent;
            }
            // Zero is the default and means "no ramp", so this is applied
            // unconditionally rather than only when a flag was passed.
            appearance_.gradient_amount = engine.GetPanelGradient();

            window_ = WindowSystem::CreateWindowSystem(WindowAPIType::WINDOW_API_GLFW);
            if (!window_)
            {
                diagnostic = "panel viewer could not create a window system";
                return false;
            }
            WindowCreateInfo window_info{};
            window_info.width = static_cast<int>(kViewerWidth);
            window_info.height = static_cast<int>(kViewerHeight);
            window_info.title = "KimPeanut Panel Viewer";
            window_info.graphics_api_type = engine.GetGraphicsAPI();
            if (!window_->Initialize(window_info))
            {
                diagnostic = "panel viewer window initialization failed";
                return false;
            }
            window_initialized_ = true;

            backend_ = graphics::RenderBackend::CreateGraphicsBackEnd(engine.GetGraphicsAPI());
            if (!backend_)
            {
                diagnostic = "panel viewer could not create its graphics backend";
                return false;
            }
            backend_->BindWindowResize(window_->resize_event_dispatcher_);
            backend_->Initialize(window_->GetNativeHandle());
            backend_initialized_ = true;

            const std::uint32_t frame_count =
                std::max(1u, static_cast<std::uint32_t>(backend_->GetFramesInFlight()));
            frame_contexts_.reserve(frame_count);
            for (std::uint32_t index = 0u; index < frame_count; ++index)
            {
                auto frame = std::make_unique<render::FrameContext>();
                frame->Initialize(*backend_, kUniformCapacity);
                frame_contexts_.push_back(std::move(frame));
            }

            renderer_ = std::make_unique<PanelRenderer>();
            // The render target is sized to the panel, not to the window. A
            // display device has its own resolution; tracking the window would
            // make the dot size a function of how large the window happens to be,
            // and would have nothing to say when the panel is placed in a scene.
            const std::uint32_t target_width =
                PanelDotWidth(panel_.Columns()) * kPixelsPerDot;
            const std::uint32_t target_height =
                PanelDotHeight(panel_.Rows()) * kPixelsPerDot;
            if (!renderer_->Initialize(*backend_, target_width, target_height, diagnostic))
            {
                return false;
            }
            render_initialized_ = true;

            if (!RefreshDotMask(diagnostic))
            {
                return false;
            }

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

            // --resize still moves the window, but the panel's target no longer
            // follows the window, so the capture is not deferred on it: there is
            // no post-resize target for the image to wait for.
            if (engine.GetStartupResize().has_value())
            {
                pending_resize_ = engine.GetStartupResize();
            }
            RequestStartupCapture();
            return true;
        }
        catch (const std::exception &error)
        {
            diagnostic = error.what();
            return false;
        }
        catch (...)
        {
            diagnostic = "unknown panel viewer initialization failure";
            return false;
        }
    }

    bool PanelViewerHost::RefreshDotMask(std::string &diagnostic)
    {
        if (!renderer_)
        {
            diagnostic = "panel renderer is not initialized";
            return false;
        }

        const DotMatrix matrix = panel_.Rebuild(glyphs_);
        // Content changes rarely and a full panel is 16 KB, so an equality check
        // is cheaper than the upload it avoids and simpler than a revision field
        // that could drift from the content it describes.
        if (mask_uploaded_ && matrix == uploaded_mask_)
        {
            return true;
        }
        if (!renderer_->UploadPanel(matrix, diagnostic))
        {
            return false;
        }
        uploaded_mask_ = matrix;
        mask_uploaded_ = true;
        return true;
    }

    bool PanelViewerHost::Tick(float delta_time, std::string &diagnostic)
    {
        (void)delta_time;
        diagnostic.clear();
        // The panel has no simulation, so there is nothing to advance. The
        // contract requires the call, and a failed one would be a state error
        // rather than a content error.
        if (!glyphs_loaded_)
        {
            diagnostic = "panel viewer has no glyph product loaded";
            return false;
        }
        return true;
    }

    void PanelViewerHost::ApplyPendingResize()
    {
        if (!pending_resize_.has_value() || window_ == nullptr)
        {
            return;
        }

        const runtime::RuntimeResizeRequest resize = *pending_resize_;
        pending_resize_.reset();

        // A real window resize, not only a recorded extent: a Vulkan backend
        // recreates its surface from what the platform granted. The panel's own
        // render target is unaffected, because it is sized to the panel.
        window_->RequestWindowSize(static_cast<int>(resize.width),
                                   static_cast<int>(resize.height));
    }

    void PanelViewerHost::ApplyPendingContent()
    {
        PendingContent pending;
        {
            std::lock_guard<std::mutex> lock(content_mutex_);
            pending.text = std::move(pending_content_.text);
            pending.appearance = pending_content_.appearance;
            pending_content_ = {};
        }

        if (pending.text.has_value())
        {
            panel_.SetText(0u, *pending.text);
        }

        if (pending.appearance.has_value())
        {
            const PanelAppearanceUpdate &update = *pending.appearance;
            // Only the fields the command set are applied, so a command can
            // change the gap without restating the colours.
            if (update.has_dot_gap)
            {
                appearance_.dot_gap = update.dot_gap;
            }
            if (update.has_dot_color)
            {
                appearance_.dot_color = update.dot_color;
            }
            if (update.has_background_color)
            {
                appearance_.background_color = update.background_color;
            }
            if (update.has_accent_color)
            {
                appearance_.accent_color = update.accent_color;
            }
            if (update.has_gradient_amount)
            {
                appearance_.gradient_amount = update.gradient_amount;
            }
            if (update.has_gradient_axis)
            {
                appearance_.gradient_axis =
                    static_cast<PanelGradientAxis>(update.gradient_axis);
            }
            if (update.has_cycles_per_second)
            {
                appearance_.cycles_per_second = update.cycles_per_second;
            }
        }
    }

    void PanelViewerHost::PublishAppliedState()
    {
        AppliedState state;
        state.appearance = appearance_;
        state.lit_dots = mask_uploaded_ ? CountLitDots(uploaded_mask_) : 0u;
        for (const char32_t codepoint : panel_.CodepointsAt(0u))
        {
            // Reported as decoded codepoints rather than as text: the command
            // payload is key/value and has to stay transportable.
            state.text += std::to_string(static_cast<std::uint32_t>(codepoint));
            state.text += " ";
        }

        std::lock_guard<std::mutex> lock(content_mutex_);
        applied_state_ = std::move(state);
    }

    bool PanelViewerHost::RecordFrame(std::string &diagnostic)
    {
        diagnostic.clear();
        if (!render_initialized_ || !window_ || !backend_ || frame_contexts_.empty() ||
            !renderer_)
        {
            diagnostic = "panel viewer render state is not initialized";
            return false;
        }

        window_->PollEvents();
        if (window_->ShouldClose())
        {
            return true;
        }

        ApplyPendingResize();

        // Applied and uploaded before the frame bracket, because the upload waits
        // for the device and that must not happen between BeginFrame and EndFrame.
        // Before this existed, a command could change the text and the panel
        // simply never re-uploaded: the change was accepted and never drawn.
        ApplyPendingContent();
        std::string upload_diagnostic;
        if (!RefreshDotMask(upload_diagnostic))
        {
            diagnostic = upload_diagnostic;
            return false;
        }
        PublishAppliedState();

        graphics::RenderBackend &backend = *backend_;
        backend.BeginFrame();

        const std::size_t slot =
            static_cast<std::size_t>(backend.GetCurrentFrameIndex()) % frame_contexts_.size();
        render::FrameContext &frame = *frame_contexts_[slot];

        const graphics::Extent2D extent = backend.GetRenderExtent();
        if (extent.width == 0u || extent.height == 0u)
        {
            backend.EndFrame();
            return true;
        }

        graphics::CommandRecorder *const recorder = backend.GetCommandRecorder();
        if (recorder == nullptr)
        {
            backend.EndFrame();
            diagnostic = "panel viewer backend exposed no command recorder";
            return false;
        }

        // Time is supplied to the planner rather than read from a clock there, so
        // planning stays a pure function of its inputs and a still ramp is
        // exactly reproducible.
        appearance_.elapsed_seconds = elapsed_seconds_;

        const render::FrameGlobals globals{frame_number_, elapsed_seconds_, 1.0f / 60.0f};
        frame.Begin(static_cast<std::uint32_t>(slot), globals, extent);
        if (!renderer_->Record(frame, *recorder, appearance_, diagnostic))
        {
            frame.End();
            backend.EndFrame();
            return false;
        }
        if (render_capture_service_)
        {
            render_capture_service_->EnqueuePendingReadback();
        }
        frame.End();
        backend.EndFrame();

        CompleteWindowCapture();

        // OpenGL presents through the window rather than the backend; Vulkan's
        // EndFrame already presented.
        if (engine_ != nullptr &&
            engine_->GetGraphicsAPI() == GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
            window_->SwapBuffers();
        }

        ++frame_number_;
        elapsed_seconds_ += 1.0f / 60.0f;
        return true;
    }

    void PanelViewerHost::CompleteWindowCapture() noexcept
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
                 std::string{"panel viewer window capture threw an exception: "} +
                     error.what()});
        }
        catch (...)
        {
            render_capture_service_->CompletePendingWindowCapture(
                {render::CaptureResultStatus::Failed, {},
                 "panel viewer window capture threw an unknown exception"});
        }
    }

    void PanelViewerHost::RequestStartupCapture()
    {
        if (engine_ == nullptr || screenshot_service_ == nullptr ||
            !engine_->GetStartupCaptureOverride().has_value())
        {
            return;
        }

        exit_after_capture_ = engine_->GetStartupExitAfterCapture();
        runtime::ScreenshotRequest request{};
        // The product view reads the panel's own output target rather than the
        // swapchain, which is what makes two backends comparable: it carries no
        // window chrome.
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
                    const DotMatrix &mask = uploaded_mask_;
                    KP_LOG("PanelViewer", LOG_LEVEL_INFO,
                           "Startup capture exported to %s (frame %llu, product %s, "
                           "dots %ux%u, lit %u, live_gpu_handles %u)",
                           result.output_path.c_str(),
                           static_cast<unsigned long long>(frame_number_),
                           loaded_glyph_product_.c_str(), mask.Width(), mask.Height(),
                           CountLitDots(mask),
                           renderer_ ? renderer_->GetLiveGpuHandleCount() : 0u);
                }
                else
                {
                    KP_LOG("PanelViewer", LOG_LEVEL_ERROR, "Startup capture failed: %s",
                           result.diagnostic.c_str());
                }
                capture_settled_ = true;
            });
    }

    bool PanelViewerHost::ShouldClose() const noexcept
    {
        if (exit_after_capture_ && capture_settled_)
        {
            return true;
        }
        return window_ != nullptr && window_->ShouldClose();
    }

    WindowSystem *PanelViewerHost::GetHostWindow() noexcept
    {
        return window_.get();
    }

    bool PanelViewerHost::RegisterHostCommands(
        runtime::command::CommandRegistry &registry, std::string &diagnostic)
    {
        PanelCommandResolvers resolvers;
        resolvers.report = [this](PanelRuntimeReport &report)
            {
                if (renderer_ == nullptr || !glyphs_loaded_)
                {
                    return false;
                }
                report.glyphs_loaded = true;
                report.glyph_product = loaded_glyph_product_;
                report.first_codepoint = glyphs_.FirstCodepoint();
                report.glyph_count = static_cast<std::uint32_t>(glyphs_.Count());
                report.columns = panel_.Columns();
                report.rows = panel_.Rows();
                report.dot_width = PanelDotWidth(panel_.Columns());
                report.dot_height = PanelDotHeight(panel_.Rows());
                report.has_dot_mask = renderer_->HasDotMask();
                report.live_gpu_handles = renderer_->GetLiveGpuHandleCount();

                // The state the render thread published, not the live panel: this
                // resolver runs on the game thread, and reading a std::string the
                // render thread may be rebuilding is a race.
                AppliedState applied;
                {
                    std::lock_guard<std::mutex> lock(content_mutex_);
                    applied = applied_state_;
                }
                report.lit_dots = applied.lit_dots;
                report.text = applied.text;
                // Reported alongside the product for the same reason: a capture
                // shows pixels and cannot say which gap, colour, or ramp made
                // them.
                report.dot_gap = applied.appearance.dot_gap;
                report.dot_color = FormatPanelColor(applied.appearance.dot_color);
                report.background_color =
                    FormatPanelColor(applied.appearance.background_color);
                report.accent_color = FormatPanelColor(applied.appearance.accent_color);
                report.gradient_amount = applied.appearance.gradient_amount;
                report.gradient_axis = FormatPanelGradientAxis(
                    static_cast<std::uint32_t>(applied.appearance.gradient_axis));
                report.cycles_per_second = applied.appearance.cycles_per_second;
                return true;
            };
        resolvers.set_text = [this](std::string_view text, std::string &set_text_diagnostic)
            {
                if (!glyphs_loaded_)
                {
                    set_text_diagnostic = "no glyph product is loaded";
                    return false;
                }
                // Recorded, not applied: this runs on the game thread and the
                // panel is read by the render thread, which applies it before its
                // next frame and owns the device upload.
                std::lock_guard<std::mutex> lock(content_mutex_);
                pending_content_.text = std::string{text};
                return true;
            };
        resolvers.set_appearance = [this](const PanelAppearanceUpdate &update, std::string &)
        {
            // Recorded rather than applied, for the same reason as set_text.
            // Nothing here fails; the command provider rejects unusable values
            // before they reach this point.
            std::lock_guard<std::mutex> lock(content_mutex_);
            pending_content_.appearance = update;
            return true;
        };

        PanelCommandRegistrationResult registration =
            RegisterPanelCommands(registry, std::move(resolvers));
        if (!registration.succeeded)
        {
            diagnostic = registration.diagnostic;
            return false;
        }
        // Holding the tokens is what keeps the entries installed; the registry
        // releases them when this host is destroyed.
        command_registrations_ = std::move(registration.registrations);
        return true;
    }

    void PanelViewerHost::CleanupGpu() noexcept
    {
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
                    KP_LOG("PanelViewer", LOG_LEVEL_WARNING,
                           "panel shutdown released with %u module GPU handles still live",
                           shutdown_leaked_handles_);
                }
                catch (...)
                {
                }
            }
        }
        renderer_.reset();
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

    void PanelViewerHost::ShutdownRenderThread() noexcept
    {
        CleanupGpu();
    }

    void PanelViewerHost::Shutdown() noexcept
    {
        CleanupGpu();
        command_registrations_.clear();
        glyphs_loaded_ = false;
        mask_uploaded_ = false;
        loaded_glyph_product_.clear();
        engine_ = nullptr;
    }
}
