#ifndef KPENGINE_RUNTIME_RENDER_RENDER_SYSTEM_H
#define KPENGINE_RUNTIME_RENDER_RENDER_SYSTEM_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "asset/asset.h"
#include "asset/asset_load_request.h"
#include "async/async_queue.h"
#include "base/event.h"
#include "base/type.h"
#include "delegate/event_dispatcher.h"
#include "graphics/backend/common/api.h"
#include "camera_source_registry.h"
#include "deferred_renderer.h"
#include "environment_source_registry.h"
#include "frame_context.h"
#include "render/light/light_source_registry.h"
#include "render/material/material_system.h"
#include "render/render_capture_service.h"
#include "render_camera.h"
#include "render_resource.h"
#include "render_source_registry.h"
#include "render_world/render_world.h"

namespace kpengine::graphics
{
    class RenderBackend;
}

namespace kpengine::resource
{
    class ResourcePipeline;
}

namespace kpengine::runtime
{
    class RuntimeContext;
}

namespace kpengine::render
{
    class RenderResourceResolver;
    class MaterialAssetResolver;
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
        async::AsyncQueue<asset::AssetLoadRequest> *load_queue = nullptr;
        RenderBackendFactory backend_factory;
    };

    // `payload` pins CPU data; `resource` is the one render-ready result for this request.
    struct RenderCacheEntry
    {
        asset::AssetID asset_id;
        asset::AssetPayload payload;
        RenderResource resource;
    };

    // The render-module facade. Reconstruction re-owns the RHI backend, the
    // resource-queue drain + render cache, and the scene graph here
    // (docs/render/overview.md). Today it consumes the async load queue in
    // two modes: a full bootstrap drain at init, and a budgeted drain per frame.
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
        // Bootstrap pass: drain every queued request now (blocking at init is fine).
        bool PostInitialize();
        // Safe to call repeatedly; the first call retires all owned state.
        void Shutdown();

        RenderSystemLifecycleState GetLifecycleState() const { return lifecycle_state_; }
        const std::string &GetLastDiagnostic() const { return last_diagnostic_; }

        // Runtime pass: drain a bounded number of requests so no frame stalls.
        bool Tick(float delta_time);
        // Split frame bracket for the editor: scene work is recorded first, then
        // the API-specific editor renderer composites before submission/present.
        bool BeginFrame(float delta_time);
        bool EndFrame();
        // The editor owns ImGui frame construction, but RenderSystem owns when
        // that external work runs: after ScenePass and before presentation.
        bool ExecuteEditorCompositePass(const std::function<void()> &record_pass);

        bool IsReady(asset::RequestID request_id) const;
        const RenderCacheEntry *GetCached(asset::RequestID request_id) const;
        graphics::PipelineHandle GetPipeline(asset::RequestID request_id) const;
        GraphicsContext GetGraphicsContext();
        const RenderTarget &GetSceneRenderTarget() const;
        // The editor provides its available viewport extent. Reallocation happens
        // at the next safe frame boundary, never while UI is reading the view.
        void RequestSceneRenderTargetExtent(uint32_t width, uint32_t height);
        // Distinct shader references loaded so far, reference-based on the content
        // hash (the ShaderCache key) so a stage shared across programs counts once.
        int GetLoadedShaderCount() const;
        IRenderableSourceSink *GetRenderableSourceSink() { return &source_registry_; }
        ILightSourceSink *GetLightSourceSink() { return &light_source_registry_; }
        ICameraSourceSink *GetCameraSourceSink() { return &camera_source_registry_; }
        IEnvironmentSourceSink *GetEnvironmentSourceSink()
        {
            return &environment_source_registry_;
        }
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

        // Drain up to max_items requests (0 = unlimited, the bootstrap pass).
        void ConsumeRequests(std::size_t max_items);
        bool ConsumeOne(const asset::AssetLoadRequest &request, RenderCacheEntry &entry);
        RenderableSourceResolution ResolveRenderableSource(
            const PrimitiveRenderableSourceDesc &source);
        MaterialResolution ResolveMaterialAsset(asset::AssetID material_asset,
                                                MaterialInstanceHandle &out_instance);
        void DrainRenderableSources();
        void DrainLightSources();
        void DrainCameraSources();
        void DrainEnvironmentSources();

        const RenderCacheEntry *FindCached(asset::AssetID asset_id) const;
        void DestroyMaterialAssetRecords();
        void ApplyDefaultCamera();
        FrameContext *GetCurrentFrameContext();

    private:
        std::unique_ptr<resource::ResourcePipeline> resource_pipeline_;
        std::unique_ptr<graphics::RenderBackend> backend_;
        std::unique_ptr<MaterialSystem> material_system_;
        std::unique_ptr<RenderResourceResolver> resource_resolver_;
        std::unique_ptr<DeferredRenderer> deferred_renderer_;
        std::vector<FrameContext> frame_contexts_;
        RenderWorld render_world_;
        RenderableSourceRegistry source_registry_;
        LightWorld light_world_;
        LightSourceRegistry light_source_registry_;
        CameraSourceRegistry camera_source_registry_;
        EnvironmentSourceRegistry environment_source_registry_;
        RenderCamera scene_camera_;
        std::unique_ptr<MaterialAssetResolver> material_asset_resolver_;
        std::unique_ptr<RenderCaptureService> render_capture_service_;
        uint64_t frame_number_ = 0;
        float elapsed_seconds_ = 0.0f;
        async::AsyncQueue<asset::AssetLoadRequest> *load_queue_ = nullptr;

        std::unordered_map<asset::RequestID, RenderCacheEntry> render_cache_;
        FrameContext *active_frame_context_ = nullptr;
        RenderSystemLifecycleState lifecycle_state_ =
            RenderSystemLifecycleState::Uninitialized;
        std::string last_diagnostic_;
        bool backend_initialized_ = false;
    };
}

#endif
