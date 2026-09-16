#ifndef KPENGINE_LIVE2D_VIEWER_HOST_H
#define KPENGINE_LIVE2D_VIEWER_HOST_H

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "command/command_registry.h"
#include "host/application_host.h"
#include "launch_options.h"
#include "render/frame_context.h"
#include "render/render_capture_service_internal.h"
#include "bubble_renderer.h"
#include "glyph_cell.h"
#include "module/live2d/render/live2d_renderer.h"
#include "panel.h"
#include "runtime/live2d_emotion_replay.h"
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
        void RenderControlPanel();
        void RenderProfilerWindow();
        void RenderDebugControls();
        void ApplyEmotionPreset(std::string_view preset);
        bool ShowEmotionBubble(std::string_view text);
        Live2DFrameInput BuildFrameInput(float delta_time);
        void HandleCursorEvent(const CursorEvent &event) noexcept;
        void CompleteWindowCapture() noexcept;
        void CleanupGpu() noexcept;
        // Applies --resize once, outside a frame bracket and before the startup
        // capture is requested, so the exported image reflects the new extent.
        void ApplyPendingResize();
        // Builds the bubble's draws for this frame, or leaves them empty when
        // there is no bubble. The tail is aimed at the model through the fitted
        // bounds the renderer publishes, so nothing Live2D-specific crosses into
        // the bubble but a direction.
        void BuildBubbleDraws(const graphics::Extent2D &extent,
                              std::vector<render::SubmissionDraw> &out);
        // Requests the startup capture once nothing is still moving: a resize may
        // be waiting for the target and a bubble for its pop-in, and either alone
        // would make the exported image a picture of a moment in between.
        void RequestCaptureWhenSettled();
        // Advances the pop-in and reports whether it has finished.
        bool AdvanceBubblePop(float delta_time);
        // Queues the one-shot startup capture named by --capture. Called either
        // during Initialize (no resize pending) or after the resize is applied.
        void RequestStartupCapture();

        runtime::Engine *engine_ = nullptr;
        std::unique_ptr<WindowSystem> window_;
        std::unique_ptr<graphics::RenderBackend> backend_;
        std::vector<std::unique_ptr<render::FrameContext>> frame_contexts_;
        Live2DSystem system_;
        std::unique_ptr<Live2DRenderer> renderer_;

        // The speech bubble, when one is asked for. It is a panel consumer: the
        // text is a panel's content, and the bubble is the shape around it. None
        // The bubble is optional at startup and is also created on demand by an
        // emotion preset, so the default viewer pays no GPU cost until needed.
        std::unique_ptr<BubbleRenderer> bubble_;
        panel::GlyphSet bubble_glyphs_{0u, {}};
        panel::Panel bubble_panel_{};
        // The text as dots, built once and kept: the bubble is sized to it and
        // the texture is uploaded from it, and rebuilding the same matrix every
        // frame would be work for an answer that does not change.
        panel::DotMatrix bubble_ink_{0u, 0u};
        // The bubble's own appearance, which is not the panel's: manga is dark on
        // light, the opposite of a lit display.
        BubbleAppearance bubble_appearance_{};
        BubblePlacement bubble_placement_{};
        panel::GlyphSet emotion_glyphs_{32u, std::vector<panel::GlyphCell>(95u)};
        bool bubble_enabled_ = false;
        bool bubble_placement_logged_ = false;
        // The pop-in, from nothing to finished. Driven here rather than by the
        // renderer so the animation is a property of the frame, not of the shape.
        float bubble_pop_ = 1.0f;
        float emotion_bubble_remaining_ = 0.0f;
        bool emotion_bubble_timed_ = false;
        // shared_ptr keeps the private UI state incomplete in this public host
        // header; ownership remains exclusive to this host.
        std::shared_ptr<Live2DViewerUiState> viewer_ui_;
        Live2DEmotionReplayResult behavior_replay_{};
        std::string emotion_status_ = "Normal";
        std::string emotion_diagnostic_;
        std::unique_ptr<render::RenderCaptureService> render_capture_service_;
        std::unique_ptr<runtime::RuntimeScreenshotService> screenshot_service_;
        asset::AssetID model_asset_{};
        // Asset-root-relative spelling of the product actually loaded, so a
        // report names its own fixture rather than echoing what was requested.
        std::string loaded_model_path_;
        // Keeps the host's command registration alive; the registry releases the
        // entry when this token is destroyed.
        runtime::command::CommandRegistration command_registration_;
        runtime::command::CommandRegistration screenshot_command_registration_;
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
        enum class GazeMode : std::uint8_t
        {
            Neutral,
            FollowMouse,
            FixedTarget
        };
        GazeMode gaze_mode_ = GazeMode::Neutral;
        Live2DVector2 fixed_gaze_target_{};
        Live2DVector2 cursor_position_{};
        Live2DVector2 viewer_image_min_{};
        Live2DVector2 viewer_image_size_{};
        Live2DVector2 last_gaze_target_{};
        bool cursor_position_valid_ = false;
        bool viewer_image_valid_ = false;
        bool paused_ = false;
        bool step_requested_ = false;
        bool reset_parameters_requested_ = false;
    };
}

#endif // KPENGINE_LIVE2D_VIEWER_HOST_H
