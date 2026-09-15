#ifndef KPENGINE_MODULE_PANEL_VIEWER_HOST_H
#define KPENGINE_MODULE_PANEL_VIEWER_HOST_H

#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "command/command_registry.h"
#include "host/application_host.h"
#include "launch_options.h"
#include "panel.h"
#include "panel_command_provider.h"
#include "panel_render_planner.h"
#include "render/frame_context.h"
#include "render/panel_renderer.h"
#include "render/render_capture_service_internal.h"
#include "screenshot/runtime_screenshot_service.h"
#include "window/window_system.h"

namespace kpengine
{
    namespace runtime
    {
        class Engine;
    }
}

namespace kpengine::panel
{
    // Standalone panel composition. It owns the viewer window, RHI backend,
    // frame contexts, glyph product, and renderer. It depends on no other module:
    // the panel is not a Live2D feature and must not become one.
    class PanelViewerHost final : public runtime::IApplicationHost
    {
    public:
        ~PanelViewerHost() override;

        const char *Name() const noexcept override { return "panel-viewer"; }
        bool Initialize(runtime::Engine &engine, std::string &diagnostic) override;
        // The panel has no simulation yet, so a tick validates state and nothing
        // more. It exists because the contract requires it, not because there is
        // per-frame logic to run.
        bool Tick(float delta_time, std::string &diagnostic) override;
        bool RecordFrame(std::string &diagnostic) override;
        bool ShouldClose() const noexcept override;
        WindowSystem *GetHostWindow() noexcept override;
        // Contributes panel.report and panel.set_text. The renderer and the
        // loaded product are created on the render thread after this
        // registration, so both providers resolve their state per dispatch.
        bool RegisterHostCommands(runtime::command::CommandRegistry &registry,
                                  std::string &diagnostic) override;
        void ShutdownRenderThread() noexcept override;
        void Shutdown() noexcept override;

    private:
        // Uploads the logical panel as a dot mask if the content changed since
        // the last upload. Returns false only on a real failure.
        bool RefreshDotMask(std::string &diagnostic);
        void ApplyPendingResize();
        void RequestStartupCapture();
        void CompleteWindowCapture() noexcept;
        void CleanupGpu() noexcept;

        runtime::Engine *engine_ = nullptr;
        std::unique_ptr<WindowSystem> window_;
        std::unique_ptr<graphics::RenderBackend> backend_;
        std::vector<std::unique_ptr<render::FrameContext>> frame_contexts_;
        std::unique_ptr<PanelRenderer> renderer_;

        // The logical panel and the glyphs it draws with. The dot matrix is the
        // derived cache, kept only so an unchanged panel is not re-uploaded.
        Panel panel_{};
        GlyphSet glyphs_{0u, {}};
        DotMatrix uploaded_mask_{0u, 0u};
        bool mask_uploaded_ = false;
        bool glyphs_loaded_ = false;
        std::string loaded_glyph_product_;
        // The panel's whole look, because it has no per-character colour yet.
        // Held here rather than in the renderer: it is a property of what the
        // viewer is asked to show, not of the GPU resources that show it.
        PanelRenderPlanOptions appearance_{};

        // Commands dispatch on the game thread while the panel, the appearance,
        // and the dot mask are all read by the render thread. A command therefore
        // records intent and the render thread applies it, the way the window
        // resize command queues and the window thread applies. Without this a
        // command assigning a std::string the render thread is reading is a data
        // race, not merely a staleness.
        struct PendingContent final
        {
            std::optional<std::string> text;
            std::optional<PanelAppearanceUpdate> appearance;
        };
        // What the render thread last actually rendered, published for the report
        // so a command never reads render-thread state directly.
        struct AppliedState final
        {
            std::string text;
            PanelRenderPlanOptions appearance{};
            std::uint32_t lit_dots = 0u;
        };

        void ApplyPendingContent();
        void PublishAppliedState();
        // Guards pending_content_ and applied_state_. Held only across a swap or a
        // small copy, never across a draw.
        std::mutex content_mutex_;
        PendingContent pending_content_;
        AppliedState applied_state_;

        std::unique_ptr<render::RenderCaptureService> render_capture_service_;
        std::unique_ptr<runtime::RuntimeScreenshotService> screenshot_service_;
        std::vector<runtime::command::CommandRegistration> command_registrations_;

        std::optional<runtime::RuntimeResizeRequest> pending_resize_;
        std::uint64_t frame_number_ = 0u;
        float elapsed_seconds_ = 0.0f;

        bool render_initialized_ = false;
        bool window_initialized_ = false;
        bool backend_initialized_ = false;
        bool capture_settled_ = false;
        bool exit_after_capture_ = false;
        std::uint32_t shutdown_leaked_handles_ = 0u;
    };
}

#endif
