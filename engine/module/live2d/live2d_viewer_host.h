#ifndef KPENGINE_LIVE2D_VIEWER_HOST_H
#define KPENGINE_LIVE2D_VIEWER_HOST_H

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "host/application_host.h"
#include "render/frame_context.h"
#include "render/render_capture_service_internal.h"
#include "module/live2d/render/live2d_renderer.h"
#include "runtime/live2d_system.h"
#include "screenshot/runtime_screenshot_service.h"
#include "window/window_system.h"

namespace kpengine
{
    namespace runtime
    {
        class Engine;
    }
}

namespace kpengine::live2d
{
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
        void ShutdownRenderThread() noexcept override;
        void Shutdown() noexcept override;

    private:
        void CompleteWindowCapture() noexcept;
        void CleanupGpu() noexcept;

        runtime::Engine *engine_ = nullptr;
        std::unique_ptr<WindowSystem> window_;
        std::unique_ptr<graphics::RenderBackend> backend_;
        std::vector<std::unique_ptr<render::FrameContext>> frame_contexts_;
        Live2DSystem system_;
        std::unique_ptr<Live2DRenderer> renderer_;
        std::unique_ptr<render::RenderCaptureService> render_capture_service_;
        std::unique_ptr<runtime::RuntimeScreenshotService> screenshot_service_;
        asset::AssetID model_asset_{};
        uint64_t frame_number_ = 0;
        float elapsed_seconds_ = 0.0f;
        bool render_initialized_ = false;
        bool window_initialized_ = false;
        bool backend_initialized_ = false;
        bool system_initialized_ = false;
    };
}

#endif // KPENGINE_LIVE2D_VIEWER_HOST_H
