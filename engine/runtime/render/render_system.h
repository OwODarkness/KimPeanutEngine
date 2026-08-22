#ifndef KPENGINE_RUNTIME_RENDER_RENDER_SYSTEM_H
#define KPENGINE_RUNTIME_RENDER_RENDER_SYSTEM_H

#include <cstddef>
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
#include "graphics/backend/common/pipeline_types.h"
#include "frame_context.h"
#include "render_target.h"
#include "render_resource.h"

namespace kpengine::asset
{
    struct ShaderProgramResource;
}

namespace kpengine::data
{
    struct MeshData;
    struct TextureData;
}

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
    class RenderScene;

    struct BootstrapSceneInfo
    {
        std::string shader_program_path;
        std::string model_path;
        std::string texture_path;

        bool IsComplete() const
        {
            return !shader_program_path.empty() && !model_path.empty() &&
                   !texture_path.empty();
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

    bool BuildDefaultPipelineDesc(asset::ShaderProgramResource &program,
                                  graphics::PipelineDesc &out_desc);

    // The render-module facade. Reconstruction re-owns the RHI backend, the
    // resource-queue drain + render cache, and the scene graph here
    // (docs/render/render_module.md). Today it consumes the async load queue in
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

        bool IsReady(asset::RequestID request_id) const;
        const RenderCacheEntry *GetCached(asset::RequestID request_id) const;
        graphics::PipelineHandle GetPipeline(asset::RequestID request_id) const;
        GraphicsContext GetGraphicsContext();
        const RenderTarget &GetSceneRenderTarget() const { return scene_render_target_; }
        // The editor provides its available viewport extent. Reallocation happens
        // at the next safe frame boundary, never while UI is reading the view.
        void RequestSceneRenderTargetExtent(uint32_t width, uint32_t height);
        void AddScene(RenderScene &scene);
        void RemoveScene(RenderScene &scene);

        // Distinct shader references loaded so far, reference-based on the content
        // hash (the ShaderCache key) so a stage shared across programs counts once.
        int GetLoadedShaderCount() const;

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

        // The stages of a loaded program whose data finished baking — what the
        // render-module reconstruction feeds into a graphics::PipelineDesc.
        // Stages that failed to compile are skipped; the pipeline must not see them.
        std::vector<asset::ShaderPtr> GetCachedShaders(
            const asset::ShaderProgramResource *program) const;
        graphics::PipelineHandle GetOrCreateDefaultPipeline(asset::AssetID program_id,
                                                            asset::ShaderProgramResource &program);
        graphics::MeshHandle GetOrCreateMesh(asset::AssetID asset_id,
                                             const data::MeshData &data);
        graphics::TextureHandle GetOrCreateTexture(asset::AssetID asset_id,
                                                   const data::TextureData &data);
        graphics::SamplerHandle GetOrCreateDefaultSampler();
        const RenderCacheEntry *FindCached(asset::AssetID asset_id) const;
        void CreateBootstrapScene();
        void ApplyPendingSceneRenderTargetExtent();
        FrameContext *GetCurrentFrameContext();
        void Shutdown();

    private:
        std::unique_ptr<resource::ResourcePipeline> resource_pipeline_;
        std::unique_ptr<graphics::RenderBackend> backend_;
        RenderTarget scene_render_target_;
        graphics::Extent2D pending_scene_render_target_extent_;
        std::vector<FrameContext> frame_contexts_;
        std::vector<RenderScene *> scenes_;
        std::unique_ptr<RenderScene> bootstrap_scene_;
        BootstrapSceneInfo bootstrap_scene_info_;
        uint64_t frame_number_ = 0;
        float elapsed_seconds_ = 0.0f;
        async::AsyncQueue<asset::AssetLoadRequest> *load_queue_ = nullptr;

        std::unordered_map<asset::RequestID, RenderCacheEntry> render_cache_;
        std::unordered_map<uint64_t, graphics::PipelineHandle> pipeline_cache_;
        std::unordered_map<uint64_t, graphics::MeshHandle> mesh_cache_;
        std::unordered_map<uint64_t, graphics::TextureHandle> texture_cache_;
        graphics::SamplerHandle default_sampler_handle_;
        FrameContext *active_frame_context_ = nullptr;
    };
}

#endif
