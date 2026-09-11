#include "live2d_viewer_host.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <exception>

#include <imgui.h>

#include "asset/asset_manager.h"
#include "config/path.h"
#include "editor/log/editor_log_component.h"
#include "editor/settings/editor_settings.h"
#include "editor/ui/editor_ui.h"
#include "engine.h"
#include "graphics/backend/common/render_backend.h"
#include "log/logger.h"
#include "live2d_settings.h"
#include "module/live2d/render/live2d_renderer.h"
#include "module/live2d/runtime/live2d_model_resource.h"
#include "render/render_capture_service_internal.h"
#include "screenshot/runtime_screenshot_service.h"
#include "runtime_global_context.h"
#include "window/window_system.h"

namespace kpengine::live2d
{
    namespace
    {
        float DisplayToLinear(const float value)
        {
            return value <= 0.04045f
                       ? value / 12.92f
                       : std::pow((value + 0.055f) / 1.055f, 2.4f);
        }
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

            const std::string model_path = GetAssetDirectory() + settings.preview_asset;
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
            std::array<float, 4> output_clear_color = window_background_color;
            if (engine.GetGraphicsAPI() == GraphicsAPIType::GRAPHICS_API_OPENGL)
            {
                for (std::size_t channel = 0u; channel < 3u; ++channel)
                {
                    output_clear_color[channel] =
                        DisplayToLinear(output_clear_color[channel]);
                }
            }
            renderer_->SetOutputClearColor(output_clear_color);
            if (!renderer_->Initialize(*backend_, window_info.width, window_info.height,
                                       diagnostic))
            {
                return false;
            }
            render_initialized_ = true;

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
                    if (view == render::CaptureView::Live2D && renderer_)
                    {
                        return renderer_->GetOutputTarget();
                    }
                    return graphics::RenderTargetHandle{};
                },
                [this] { return frame_number_; });
            screenshot_service_ = std::make_unique<runtime::RuntimeScreenshotService>(
                *render_capture_service_);

            if (engine.GetStartupCaptureOverride().has_value())
            {
                runtime::ScreenshotRequest request{};
                // The standalone viewer presents directly to the swapchain;
                // capture the presentation boundary so the exported image is
                // the image visible in the viewer, not the retained clear-only
                // offscreen target.
                request.capture.view = render::CaptureView::EngineWindow;
                request.output_path = *engine.GetStartupCaptureOverride();
                screenshot_service_->RequestScreenshot(
                    std::move(request),
                    [this](runtime::ScreenshotResult result)
                    {
                        if (result.IsSuccess())
                        {
                            KP_LOG("Live2DViewer", LOG_LEVEL_INFO,
                                   "Startup capture exported to %s",
                                   result.output_path.c_str());
                        }
                        else
                        {
                            KP_LOG("Live2DViewer", LOG_LEVEL_ERROR,
                                   "Startup capture failed: %s",
                                   result.diagnostic.c_str());
                        }
                    });
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
        if (!renderer_->ResizeOutput(extent.width, extent.height, diagnostic))
        {
            backend_->EndFrame();
            return false;
        }
        graphics::CommandRecorder *const recorder = backend_->GetCommandRecorder();
        if (recorder == nullptr)
        {
            backend_->EndFrame();
            return true;
        }
        elapsed_seconds_ += 1.0f / 120.0f;
        frame.Begin(frame_index, {frame_number_++, elapsed_seconds_, 1.0f / 120.0f}, extent);
        const auto render_started = std::chrono::steady_clock::now();
        if (!renderer_->Record(frame, *recorder, 1.0f / 120.0f, diagnostic))
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
        return window_ != nullptr && window_->ShouldClose();
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
            viewer_ui_->ui->DrawRenderTarget(renderer_->GetOutputView(), image_size);
        }
        ImGui::End();

        if (viewer_ui_ && viewer_ui_->log)
        {
            viewer_ui_->log->Render();
        }
        RenderProfilerWindow();
    }

    void Live2DViewerHost::RenderProfilerWindow()
    {
        ImGuiViewport *const viewport = ImGui::GetMainViewport();
        const ImVec2 profiler_pos(viewport->WorkPos.x + viewport->WorkSize.x * 0.70f,
                                  viewport->WorkPos.y);
        const ImVec2 profiler_size(viewport->WorkSize.x * 0.30f,
                                   viewport->WorkSize.y * 0.75f);
        ImGui::SetNextWindowPos(profiler_pos, ImGuiCond_Always);
        ImGui::SetNextWindowSize(profiler_size, ImGuiCond_Always);
        constexpr ImGuiWindowFlags kProfilerFlags =
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoCollapse;
        if (ImGui::Begin("Performance Profiler", nullptr, kProfilerFlags))
        {
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
