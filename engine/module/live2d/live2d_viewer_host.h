#ifndef KPENGINE_LIVE2D_VIEWER_HOST_H
#define KPENGINE_LIVE2D_VIEWER_HOST_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "command/command_registry.h"
#include "host/application_host.h"
#include "launch_options.h"
#include "render/frame_context.h"
#include "render/render_capture_service_internal.h"
#include "module/live2d/render/live2d_renderer.h"
#include "runtime/live2d_system.h"
#include "screenshot/runtime_screenshot_service.h"
#include "window/window_system.h"

namespace kpengine
{
    namespace editor
    {
        class EditorUI;
        class EditorLogComponent;
    }
    namespace runtime
    {
        class Engine;
    }
}

namespace kpengine::live2d
{
    struct Live2DViewerUiState;

    // Standalone Live2D composition. It owns the viewer window, RHI backend,
    // frame contexts, Cubism service, and renderer; it does not construct or
    // depend on RuntimeContext/RenderWorld/DeferredRenderer.
    class Live2DViewerHost final : public runtime::IApplicationHost
    {
    public:
        ~Live2DViewerHost() override;

        const char *Name() const noexcept override { return "live2d-viewer"; }
        bool Initialize(runtime::Engine &engine, std::string &diagnostic) override;
        bool Tick(float delta_time, std::string &diagnostic) override;
        bool RecordFrame(std::string &diagnostic) override;
        bool ShouldClose() const noexcept override;
        // The viewer owns its window; Runtime's shared window system is unused
        // in this mode, so a Runtime command must resolve the window through here.
        WindowSystem *GetHostWindow() noexcept override;
        // Contributes live2d.model_report. The renderer, and with it the loaded
        // product, is created on the render thread after this registration, so
        // the provider resolves both per dispatch.
        bool RegisterHostCommands(runtime::command::CommandRegistry &registry,
                                  std::string &diagnostic) override;
        void ShutdownRenderThread() noexcept override;
        void Shutdown() noexcept override;

    private:
        void RenderViewerUI();
        void RenderProfilerWindow();
        void CompleteWindowCapture() noexcept;
        void CleanupGpu() noexcept;
        // Applies --resize once, outside a frame bracket and before the startup
        // capture is requested, so the exported image reflects the new extent.
        void ApplyPendingResize();
        // Queues the one-shot startup capture named by --capture. Called either
        // during Initialize (no resize pending) or after the resize is applied.
        void RequestStartupCapture();

        runtime::Engine *engine_ = nullptr;
        std::unique_ptr<WindowSystem> window_;
        std::unique_ptr<graphics::RenderBackend> backend_;
        std::vector<std::unique_ptr<render::FrameContext>> frame_contexts_;
        Live2DSystem system_;
        std::unique_ptr<Live2DRenderer> renderer_;
        // shared_ptr keeps the private UI state incomplete in this public host
        // header; ownership remains exclusive to this host.
        std::shared_ptr<Live2DViewerUiState> viewer_ui_;
        std::unique_ptr<render::RenderCaptureService> render_capture_service_;
        std::unique_ptr<runtime::RuntimeScreenshotService> screenshot_service_;
        asset::AssetID model_asset_{};
        // Asset-root-relative spelling of the product actually loaded, so a
        // report names its own fixture rather than echoing what was requested.
        std::string loaded_model_path_;
        // Keeps the host's command registration alive; the registry releases the
        // entry when this token is destroyed.
        runtime::command::CommandRegistration command_registration_;
        // Pending --resize extent. Applied after the first recorded frame; the
        // startup capture is requested only once the output target carries the
        // new extent, so the exported image's dimensions are the evidence.
        std::optional<runtime::RuntimeResizeRequest> pending_resize_;
        runtime::RuntimeResizeRequest applied_resize_{};
        // Set while the startup capture is waiting for a resize to reach the
        // output target. Bounded by kResizeWaitFrameBudget so a resize that
        // never lands cannot leave the run waiting forever.
        bool capture_waits_for_resize_ = false;
        uint32_t resize_wait_frames_ = 0u;
        uint64_t frame_number_ = 0;
        float elapsed_seconds_ = 0.0f;
        double game_tick_work_ms_ = 0.0;
        double render_work_ms_ = 0.0;
        double imgui_work_ms_ = 0.0;
        double frame_total_ms_ = 0.0;
        bool render_initialized_ = false;
        bool window_initialized_ = false;
        bool backend_initialized_ = false;
        bool system_initialized_ = false;
        // Set while the last output-target resize failed; the host keeps using
        // the previous target and only reports the transition once.
        bool output_resize_failed_ = false;
        unsigned shutdown_leaked_handles_ = 0u;
        // Set once the startup capture resolves, so --exit-after-capture can
        // reach the shutdown path instead of the process being killed.
        bool capture_settled_ = false;
        bool exit_after_capture_ = false;
    };
}

#endif // KPENGINE_LIVE2D_VIEWER_HOST_H
