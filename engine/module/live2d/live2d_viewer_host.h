#ifndef KPENGINE_LIVE2D_VIEWER_HOST_H
#define KPENGINE_LIVE2D_VIEWER_HOST_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "asset/asset.h"
#include "host/application_host.h"
#include "launch_options.h"
#include "math/math_header.h"

namespace kpengine
{
    struct CursorEvent;
    class WindowSystem;
    namespace graphics
    {
        class RenderBackend;
    }
    namespace render
    {
        class FrameContext;
        class RenderCaptureService;
    }
    namespace runtime
    {
        class Engine;
        class RuntimeScreenshotService;
        namespace command
        {
            class CommandRegistry;
        }
    }
}

namespace kpengine::live2d
{
    template <typename T>
    struct Live2DViewerHostDeleter final
    {
        void operator()(T *object) const noexcept;
    };

    template <typename T>
    using Live2DViewerHostPtr =
        std::unique_ptr<T, Live2DViewerHostDeleter<T>>;

    class Live2DEmotionRuntime;
    class Live2DRenderer;
    class Live2DSystem;
    namespace editor
    {
        class Live2DViewerBubble;
        class Live2DViewerCommandBridge;
        class Live2DViewerEditor;
        struct Live2DViewerEditorState;
        enum class Live2DGazeMode : std::uint8_t;
    }
    struct Live2DFrameInput;

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
        void ApplyEmotionPreset(std::string_view preset);
        bool ShowEmotionBubble(std::string_view text, std::string &diagnostic);
        Live2DFrameInput BuildFrameInput(float delta_time);
        editor::Live2DViewerEditorState BuildEditorState() const;
        void HandleCursorEvent(const CursorEvent &event) noexcept;
        void CompleteWindowCapture() noexcept;
        void CleanupGpu() noexcept;
        // Applies --resize once, outside a frame bracket and before the startup
        // capture is requested, so the exported image reflects the new extent.
        void ApplyPendingResize();
        // Requests the startup capture once nothing is still moving: a resize may
        // be waiting for the target and a bubble for its pop-in, and either alone
        // would make the exported image a picture of a moment in between.
        void RequestCaptureWhenSettled();
        // Queues the one-shot startup capture named by --capture. Called either
        // during Initialize (no resize pending) or after the resize is applied.
        void RequestStartupCapture();

        runtime::Engine *engine_ = nullptr;
        Live2DViewerHostPtr<WindowSystem> window_;
        Live2DViewerHostPtr<graphics::RenderBackend> backend_;
        std::vector<Live2DViewerHostPtr<render::FrameContext>> frame_contexts_;
        Live2DViewerHostPtr<Live2DSystem> system_;
        Live2DViewerHostPtr<Live2DRenderer> renderer_;

        // The speech bubble, when one is asked for. It is a panel consumer: the
        // text is a panel's content, and the bubble is the shape around it. None
        // The bubble is optional at startup and is also created on demand by an
        // emotion preset, so the default viewer pays no GPU cost until needed.
        Live2DViewerHostPtr<editor::Live2DViewerBubble> bubble_;
        Live2DViewerHostPtr<editor::Live2DViewerEditor> viewer_editor_;
        Live2DViewerHostPtr<editor::Live2DViewerCommandBridge> command_bridge_;
        Live2DViewerHostPtr<Live2DEmotionRuntime> emotion_runtime_;
        Live2DViewerHostPtr<render::RenderCaptureService> render_capture_service_;
        Live2DViewerHostPtr<runtime::RuntimeScreenshotService> screenshot_service_;
        asset::AssetID model_asset_{};
        // Asset-root-relative spelling of the product actually loaded, so a
        // report names its own fixture rather than echoing what was requested.
        std::string loaded_model_path_;
        // Pending --resize extent. Applied after the first recorded frame; the
        // startup capture is requested only once the output target carries the
        // new extent, so the exported image's dimensions are the evidence.
        std::optional<runtime::RuntimeResizeRequest> pending_resize_;
        runtime::RuntimeResizeRequest applied_resize_{};
        // Set while the startup capture is waiting for a resize to reach the
        // output target. Bounded by kResizeWaitFrameBudget so a resize that
        // never lands cannot leave the run waiting forever.
        bool capture_waits_for_resize_ = false;
        // Set while a startup capture is waiting for the bubble's pop-in to
        // finish. A capture taken mid-pop records a bubble that is still growing,
        // which is a true image of a moment nobody asked for.
        bool capture_waits_for_bubble_ = false;
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
        editor::Live2DGazeMode gaze_mode_{};
        Vector2f fixed_gaze_target_{};
        Vector2f cursor_position_{};
        Vector2f viewer_image_min_{};
        Vector2f viewer_image_size_{};
        Vector2f last_gaze_target_{};
        bool cursor_position_valid_ = false;
        bool viewer_image_valid_ = false;
        bool paused_ = false;
        bool step_requested_ = false;
        bool reset_parameters_requested_ = false;
    };
}

#endif // KPENGINE_LIVE2D_VIEWER_HOST_H
