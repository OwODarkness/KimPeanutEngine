#ifndef KPENGINE_RUNTIME_RENDER_RENDER_SYSTEM_H
#define KPENGINE_RUNTIME_RENDER_RENDER_SYSTEM_H

#include <atomic>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>
#include <chrono>

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
#include "render_profile.h"
#include "path_trace_probe_mode.h"
#include "path_trace_settings.h"

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
        PresentationReady,
        Ready,
        FrameActive,
        ShutDown,
    };

    enum class DebugViewConsumer : uint8_t
    {
        EditorDebugViewer,
        RuntimeTooling,
        Count,
    };

    struct RenderSystemInitResult
    {
        bool success = false;
        std::string diagnostic;

        explicit operator bool() const { return success; }
    };

    enum class PreparedAssetsUpdateStatus : std::uint8_t
    {
        Unknown,
        Pending,
        Applied,
        Failed,
        Superseded,
    };

    struct PreparedAssetsUpdateResult
    {
        PreparedAssetsUpdateStatus status = PreparedAssetsUpdateStatus::Unknown;
        std::string diagnostic;
    };

    using RenderBackendFactory =
        std::function<std::unique_ptr<graphics::RenderBackend>(GraphicsAPIType)>;

    struct RenderSystemInitInfo
    {
        GraphicsAPIType api_type = GraphicsAPIType::GRAPHICS_API_UNKNOW;
        bool ray_tracing_enabled = true;
        bool path_tracing_enabled = true;
        WindowHandle native_window = nullptr;
        EventDispatcher<ResizeEvent> *resize_dispatcher = nullptr;
        std::shared_ptr<const PreparedRenderAssetCatalog> prepared_assets;
        RenderBackendFactory backend_factory;
        // Runtime supplies the window-layer capture operation. RenderSystem
        // invokes it at the backend's final presentation boundary.
        std::function<CaptureResult()> window_capture;
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
        RenderSystemInitResult InitializePresentation(const RenderSystemInitInfo &info);
        RenderSystemInitResult PromoteToScene(
            std::shared_ptr<const PreparedRenderAssetCatalog> prepared_assets);
        uint64_t QueuePreparedAssetsUpdate(
            std::shared_ptr<const PreparedRenderAssetCatalog> prepared_assets);
        // Publishes CPU shader artifacts to Editor presentation at a frame
        // boundary without promoting the scene renderer.
        void QueueEditorPresentationAssets(
            std::shared_ptr<const PreparedRenderAssetCatalog> prepared_assets);
        uint64_t GetAppliedPreparedAssetsUpdate() const noexcept
        { return applied_catalog_update_.load(std::memory_order_acquire); }
        PreparedAssetsUpdateResult GetPreparedAssetsUpdateResult(uint64_t serial) const;
        // Safe to call repeatedly; the first call retires all owned state.
        void Shutdown();

        RenderSystemLifecycleState GetLifecycleState() const { return lifecycle_state_; }
        const std::string &GetLastDiagnostic() const { return last_diagnostic_; }
        // Asset startup uses this API-neutral result to choose exactly one
        // material texture profile before resolving Material dependencies.
        bool SupportsCompleteBlockCompressedTextureProfile() const noexcept;

        // Split frame bracket for the editor: scene work is recorded first, then
        // the API-specific editor renderer composites before submission/present.
        bool BeginFrame(float delta_time);
        bool EndFrame();
        // Runtime calls this after an external window-system swap (OpenGL).
        // Vulkan reports present time from its backend EndFrame bracket.
        void RecordPresentationTime(double milliseconds);
        // Completes a pending EngineWindow capture at the presentation
        // boundary. Must be called by the render thread that owns the window
        // and graphics API.
        bool CompletePendingWindowCapture();
        // The editor owns ImGui frame construction, but RenderSystem owns when
        // that external work runs: after ScenePass and before presentation.
        bool ExecuteEditorCompositePass(const std::function<void()> &record_pass);

        graphics::RenderTargetView GetSceneRenderTargetView() const;
        std::optional<spatial::Ray> BuildSceneRay(float ndc_x, float ndc_y,
                                                  float viewport_aspect) const;
        // Tooling projection helper. The returned coordinates are NDC and the
        // camera remains private to Render; Editor converts NDC to its image rect.
        std::optional<Vector3f> ProjectScenePoint(const Vector3f &world_point,
                                                  float viewport_aspect) const;
        // Legacy single-consumer adapter. New callers should scope their request.
        void SetDebugView(CaptureView view);
        void SetDebugViewDemand(DebugViewConsumer consumer,
                                std::optional<CaptureView> view);
        bool RequestPathTraceSettings(PathTraceSettings settings);
        bool RequestScreenSpaceAoSettings(ScreenSpaceAoSettings settings);
        void RequestPathTraceProbeMode(PathTraceProbeMode mode);
        void RequestPathTraceDispatchFailureInjection() noexcept;
        CaptureView GetDebugView() const;
        graphics::RenderTargetView GetDebugRenderTargetView() const;
        // The editor provides its available viewport extent. Reallocation happens
        // at the next safe frame boundary, never while UI is reading the view.
        void RequestSceneRenderTargetExtent(uint32_t width, uint32_t height);
        struct RenderSystemMetrics
        {
            uint32_t prepared_shader_count = 0;
            uint64_t triangle_count = 0;
            std::optional<float> gpu_usage_percent;
            RenderProfileSnapshot profile;
        };
        RenderSystemMetrics GetMetrics() const;
        // Returns the last fully completed frame without racing the render thread.
        RenderSystemMetrics GetPublishedMetrics() const;
        IRenderableSourceSink *GetRenderableSourceSink()
        { return scene_coordinator_.GetRenderableSourceSink(); }
        ILightSourceSink *GetLightSourceSink()
        { return scene_coordinator_.GetLightSourceSink(); }
        ICameraSourceSink *GetCameraSourceSink()
        { return scene_coordinator_.GetCameraSourceSink(); }
        IEnvironmentSourceSink *GetEnvironmentSourceSink()
        { return scene_coordinator_.GetEnvironmentSourceSink(); }
        graphics::IEditorPresentationBridge *GetEditorPresentationBridge();
        // Borrowed immutable CPU shader catalog, available once render assets
        // have been prepared. Editor presentation can initialize optional UI
        // pipelines without taking ownership of Asset or Graphics resources.
        std::shared_ptr<const PreparedRenderAssetCatalog>
        GetEditorPresentationAssets() const;
        // Borrowed Runtime/tooling boundary. RenderSystem owns the implementation
        // and cancels any pending request before this object is destroyed.
        IRenderCaptureService *GetRenderCaptureService();
    private:
        // RuntimeContext is the normal composition root. The public lifecycle
        // functions remain directly callable so orchestration can be tested with
        // an injected existing RenderBackend factory.
        friend class runtime::RuntimeContext;

        void CleanupOwnedState();
        void CleanupSceneState();
        bool IsState(RenderSystemLifecycleState expected) const;
        void ObserveProfileFrame();
        void PublishMetricsSnapshot();

        FrameContext *GetCurrentFrameContext();

    private:
        std::unique_ptr<graphics::RenderBackend> backend_;
        std::unique_ptr<MaterialSystem> material_system_;
        std::unique_ptr<RenderResourceResolver> resource_resolver_;
        std::unique_ptr<DeferredRenderer> deferred_renderer_;
        std::vector<FrameContext> frame_contexts_;
        RenderSceneCoordinator scene_coordinator_;
        std::shared_ptr<const PreparedRenderAssetCatalog> prepared_assets_;
        std::shared_ptr<const PreparedRenderAssetCatalog> editor_presentation_assets_;
        std::unique_ptr<RenderCaptureService> render_capture_service_;
        std::function<CaptureResult()> window_capture_;
        uint64_t frame_number_ = 0;
        float elapsed_seconds_ = 0.0f;
        FrameContext *active_frame_context_ = nullptr;
        RenderSystemLifecycleState lifecycle_state_ =
            RenderSystemLifecycleState::Uninitialized;
        std::string last_diagnostic_;
        bool backend_initialized_ = false;
        bool ray_tracing_enabled_ = true;
        bool path_tracing_enabled_ = true;
        RenderSystemLifecycleState frame_return_state_ =
            RenderSystemLifecycleState::Uninitialized;
        RenderProfileSnapshot profile_;
        std::atomic<std::shared_ptr<const RenderSystemMetrics>> published_metrics_;
        RenderProfileWindow profile_window_{GetSponzaProfileScenario().warmup_frames,
                                            GetSponzaProfileScenario().sample_frames};
        std::chrono::steady_clock::time_point profile_frame_start_{};
        bool profile_scene_seen_ = false;
        CaptureView debug_view_ = CaptureView::SceneColor;
        mutable std::mutex request_mutex_;
        std::shared_ptr<const PreparedRenderAssetCatalog>
            pending_editor_presentation_assets_;
        std::array<std::optional<CaptureView>,
                   static_cast<std::size_t>(DebugViewConsumer::Count)> debug_view_demands_{};
        PathTraceSettings requested_path_trace_settings_{};
        std::optional<PathTraceSettings> pending_path_trace_settings_;
        std::optional<ScreenSpaceAoSettings> pending_screen_space_ao_settings_;
        std::optional<std::pair<uint64_t,
            std::shared_ptr<const PreparedRenderAssetCatalog>>> pending_catalog_update_;
        std::map<uint64_t, PreparedAssetsUpdateResult> catalog_update_results_;
        std::atomic<uint64_t> applied_catalog_update_{0};
        uint64_t next_catalog_update_ = 1;
        std::atomic<bool> requested_profile_window_reset_{false};
        std::atomic<bool> requested_path_trace_dispatch_failure_{false};
    };
}

#endif
