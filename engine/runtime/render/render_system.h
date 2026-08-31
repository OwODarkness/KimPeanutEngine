#ifndef KPENGINE_RUNTIME_RENDER_RENDER_SYSTEM_H
#define KPENGINE_RUNTIME_RENDER_RENDER_SYSTEM_H

#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
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
#include "frame_context.h"
#include "render/light/light_source_registry.h"
#include "render/material/material_system.h"
#include "render/render_capture_service.h"
#include "render_camera.h"
#include "render_pass.h"
#include "render_target.h"
#include "render_resource.h"
#include "render_source_registry.h"
#include "render_world/render_world.h"
#include "renderer_frame_targets.h"

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

    struct BootstrapSceneObjectInfo
    {
        std::string model_path;
        std::string material_path;
        Transform3f world_transform;

        bool IsComplete() const
        {
            return !model_path.empty() && !material_path.empty();
        }
    };

    struct BootstrapSceneInfo
    {
        std::string model_path;
        std::string material_path;
        std::string environment_path;
        Transform3f world_transform;
        std::vector<BootstrapSceneObjectInfo> objects;

        bool IsComplete() const
        {
            return !model_path.empty() && !material_path.empty();
        }
    };

    struct RenderSystemInitInfo
    {
        GraphicsAPIType api_type = GraphicsAPIType::GRAPHICS_API_UNKNOW;
        WindowHandle native_window = nullptr;
        EventDispatcher<ResizeEvent> *resize_dispatcher = nullptr;
        async::AsyncQueue<asset::AssetLoadRequest> *load_queue = nullptr;
        BootstrapSceneInfo bootstrap_scene;
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

        // Runtime pass: drain a bounded number of requests so no frame stalls.
        void Tick(float delta_time);
        // Split frame bracket for the editor: scene work is recorded first, then
        // the API-specific editor renderer composites before submission/present.
        void BeginFrame(float delta_time);
        void EndFrame();
        // The editor owns ImGui frame construction, but RenderSystem owns when
        // that external work runs: after ScenePass and before presentation.
        bool ExecuteEditorCompositePass(const std::function<void()> &record_pass);

        bool IsReady(asset::RequestID request_id) const;
        const RenderCacheEntry *GetCached(asset::RequestID request_id) const;
        graphics::PipelineHandle GetPipeline(asset::RequestID request_id) const;
        GraphicsContext GetGraphicsContext();
        const RenderTarget &GetSceneRenderTarget() const { return *frame_targets_.GetTarget(RenderTargetName::SceneColor); }
        // The editor provides its available viewport extent. Reallocation happens
        // at the next safe frame boundary, never while UI is reading the view.
        void RequestSceneRenderTargetExtent(uint32_t width, uint32_t height);
        // Distinct shader references loaded so far, reference-based on the content
        // hash (the ShaderCache key) so a stage shared across programs counts once.
        int GetLoadedShaderCount() const;
        IRenderableSourceSink *GetRenderableSourceSink() { return &source_registry_; }
        ILightSourceSink *GetLightSourceSink() { return &light_source_registry_; }
        ICameraSourceSink *GetCameraSourceSink() { return &camera_source_registry_; }
        // Borrowed Runtime/tooling boundary. RenderSystem owns the implementation
        // and cancels any pending request before this object is destroyed.
        IRenderCaptureService *GetRenderCaptureService();
        // Transfers startup renderables as logical source data. Runtime consumes
        // them on the game thread to create regular Gameplay Actors.
        std::optional<StaticMeshRenderableSourceDesc> TakeBootstrapRenderableSource();
        std::vector<StaticMeshRenderableSourceDesc> TakeBootstrapRenderableSources();

    private:
        // Lifecycle belongs to RuntimeContext: it owns this object and is the only
        // caller allowed to bring it up, so no one else can re-initialize it.
        friend class runtime::RuntimeContext;

        // Owns the resource pipeline and takes the load queue it drains.
        void Initialize(const RenderSystemInitInfo &info);
        // Bootstrap pass: drain every queued request now (blocking at init is fine).
        void PostInitialize();

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

        const RenderCacheEntry *FindCached(asset::AssetID asset_id) const;
        void PrepareBootstrapRenderableSources();
        void DestroyMaterialAssetRecords();
        void ConfigurePassSchedule();
        void RecordDirectionalShadowPass();
        void RecordGBufferPass();
        void RecordDeferredLightingPass();
        void RecordGBufferDebugViewPass();
        void RecordToneMapPass();
        void RecordPendingCapturePass();
        bool RecordCaptureViewPass(CaptureView view);
        bool PrepareDirectionalShadowPassResources();
        bool PrepareFullscreenPassResources();
        bool PrepareDeferredLightingPassResources();
        bool PrepareGBufferDebugPassResources();
        bool PrepareToneMapPassResources();
        bool PrepareCaptureViewPassResources();
        struct DirectionalShadowFrame
        {
            ShadowJobDesc job;
            ShadowHandle shadow;
            Vector3f light_direction;
            Matrix4f view;
            Matrix4f projection;
        };
        std::optional<DirectionalShadowFrame> ScheduleDirectionalShadow(
            const std::vector<Light> &lights) const;
        void RecordShadowCaster(const MeshProxy &proxy, const graphics::PerPassData &per_pass_data,
                                graphics::CommandRecorder &recorder);
        void RecordMeshProxy(const MeshProxy &proxy, const graphics::PerPassData &per_pass_data,
                             graphics::CommandRecorder &recorder, MaterialPass pass);
        void ApplyPendingSceneRenderTargetExtent();
        void ApplyDefaultCamera();
        FrameContext *GetCurrentFrameContext();
        void Shutdown();

    private:
        std::unique_ptr<resource::ResourcePipeline> resource_pipeline_;
        std::unique_ptr<graphics::RenderBackend> backend_;
        std::unique_ptr<MaterialSystem> material_system_;
        std::unique_ptr<RenderResourceResolver> resource_resolver_;
        RendererFrameTargets frame_targets_;
        RenderPassSchedule pass_schedule_;
        graphics::Extent2D pending_scene_render_target_extent_;
        std::vector<FrameContext> frame_contexts_;
        RenderWorld render_world_;
        RenderableSourceRegistry source_registry_;
        LightWorld light_world_;
        LightSourceRegistry light_source_registry_;
        CameraSourceRegistry camera_source_registry_;
        FrameLightingBinding frame_lighting_binding_;
        std::optional<DirectionalShadowFrame> active_directional_shadow_;
        RenderCamera scene_camera_;
        std::vector<StaticMeshRenderableSourceDesc> bootstrap_renderable_sources_;
        std::unique_ptr<MaterialAssetResolver> material_asset_resolver_;
        std::unique_ptr<RenderCaptureService> render_capture_service_;
        BootstrapSceneInfo bootstrap_scene_info_;
        uint64_t frame_number_ = 0;
        float elapsed_seconds_ = 0.0f;
        async::AsyncQueue<asset::AssetLoadRequest> *load_queue_ = nullptr;

        std::unordered_map<asset::RequestID, RenderCacheEntry> render_cache_;
        FrameContext *active_frame_context_ = nullptr;
        bool editor_composite_recorded_ = false;
        // Lazily built fullscreen resources. Pipelines remain pass-specific;
        // the immutable triangle mesh and sampler are shared by Render passes.
        graphics::PipelineHandle deferred_lighting_pipeline_;
        graphics::PipelineHandle gbuffer_debug_pipeline_;
        graphics::PipelineHandle capture_view_pipeline_;
        graphics::MeshHandle gbuffer_debug_fullscreen_mesh_;
        graphics::SamplerHandle gbuffer_debug_sampler_;
        graphics::SamplerHandle directional_shadow_sampler_;
        TextureBinding environment_texture_binding_;
        graphics::PipelineHandle tone_map_pipeline_;
        graphics::PipelineHandle directional_shadow_pipeline_;
    };
}

#endif
