#ifndef KPENGINE_RUNTIME_RENDER_RENDER_SYSTEM_H
#define KPENGINE_RUNTIME_RENDER_RENDER_SYSTEM_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "base/event.h"
#include "base/type.h"
#include "delegate/event_dispatcher.h"
#include "graphics/backend/common/api.h"
#include "deferred_renderer.h"
#include "frame_context.h"
#include "render/material/material_system.h"
#include "render/render_capture_service.h"
#include "render_resource.h"
#include "prepared_render_asset_catalog.h"
#include "render_scene_coordinator.h"

namespace kpengine::graphics
{
    class RenderBackend;
}

namespace kpengine::runtime
{
    class RuntimeContext;
}

namespace kpengine::render
{
    class RenderResourceResolver;
    class RenderCaptureService;

    enum class RenderSystemLifecycleState : uint8_t
    {
        Uninitialized,
        Ready,
        FrameActive,
        ShutDown,
    };

    struct RenderSystemInitResult
    {
        bool success = false;
        std::string diagnostic;

        explicit operator bool() const { return success; }
    };

    using RenderBackendFactory =
        std::function<std::unique_ptr<graphics::RenderBackend>(GraphicsAPIType)>;

    struct RenderSystemInitInfo
    {
        GraphicsAPIType api_type = GraphicsAPIType::GRAPHICS_API_UNKNOW;
        WindowHandle native_window = nullptr;
        EventDispatcher<ResizeEvent> *resize_dispatcher = nullptr;
        std::shared_ptr<const PreparedRenderAssetCatalog> prepared_assets;
        RenderBackendFactory backend_factory;
    };

    // The render-module facade. It owns frame/lifecycle orchestration while
    // RenderSceneCoordinator owns Gameplay source inboxes and scene policy.
    class RenderSystem
    {
    public:
        RenderSystem();
        ~RenderSystem();
        RenderSystem(const RenderSystem &) = delete;
        RenderSystem &operator=(const RenderSystem &) = delete;
        RenderSystem(RenderSystem &&) = delete;
        RenderSystem &operator=(RenderSystem &&) = delete;

        RenderSystemInitResult Initialize(const RenderSystemInitInfo &info);
        // Safe to call repeatedly; the first call retires all owned state.
        void Shutdown();

        RenderSystemLifecycleState GetLifecycleState() const { return lifecycle_state_; }
        const std::string &GetLastDiagnostic() const { return last_diagnostic_; }

        // Split frame bracket for the editor: scene work is recorded first, then
        // the API-specific editor renderer composites before submission/present.
        bool BeginFrame(float delta_time);
        bool EndFrame();
        // The editor owns ImGui frame construction, but RenderSystem owns when
        // that external work runs: after ScenePass and before presentation.
        bool ExecuteEditorCompositePass(const std::function<void()> &record_pass);

        graphics::RenderTargetView GetSceneRenderTargetView() const;
        // The editor provides its available viewport extent. Reallocation happens
        // at the next safe frame boundary, never while UI is reading the view.
        void RequestSceneRenderTargetExtent(uint32_t width, uint32_t height);
        struct RenderSystemMetrics
        {
            uint32_t prepared_shader_count = 0;
        };
        RenderSystemMetrics GetMetrics() const;
        IRenderableSourceSink *GetRenderableSourceSink()
        { return scene_coordinator_.GetRenderableSourceSink(); }
        ILightSourceSink *GetLightSourceSink()
        { return scene_coordinator_.GetLightSourceSink(); }
        ICameraSourceSink *GetCameraSourceSink()
        { return scene_coordinator_.GetCameraSourceSink(); }
        IEnvironmentSourceSink *GetEnvironmentSourceSink()
        { return scene_coordinator_.GetEnvironmentSourceSink(); }
        graphics::IEditorPresentationBridge *GetEditorPresentationBridge();
        // Borrowed Runtime/tooling boundary. RenderSystem owns the implementation
        // and cancels any pending request before this object is destroyed.
        IRenderCaptureService *GetRenderCaptureService();
    private:
        // RuntimeContext is the normal composition root. The public lifecycle
        // functions remain directly callable so orchestration can be tested with
        // an injected existing RenderBackend factory.
        friend class runtime::RuntimeContext;

        void CleanupOwnedState();
        bool IsState(RenderSystemLifecycleState expected) const;

        FrameContext *GetCurrentFrameContext();

    private:
        std::unique_ptr<graphics::RenderBackend> backend_;
        std::unique_ptr<MaterialSystem> material_system_;
        std::unique_ptr<RenderResourceResolver> resource_resolver_;
        std::unique_ptr<DeferredRenderer> deferred_renderer_;
        std::vector<FrameContext> frame_contexts_;
        RenderSceneCoordinator scene_coordinator_;
        std::shared_ptr<const PreparedRenderAssetCatalog> prepared_assets_;
        std::unique_ptr<RenderCaptureService> render_capture_service_;
        uint64_t frame_number_ = 0;
        float elapsed_seconds_ = 0.0f;
        FrameContext *active_frame_context_ = nullptr;
        RenderSystemLifecycleState lifecycle_state_ =
            RenderSystemLifecycleState::Uninitialized;
        std::string last_diagnostic_;
        bool backend_initialized_ = false;
    };
}

#endif
