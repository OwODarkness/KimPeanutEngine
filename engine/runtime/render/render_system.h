#ifndef KPENGINE_RUNTIME_RENDER_RENDER_SYSTEM_H
#define KPENGINE_RUNTIME_RENDER_RENDER_SYSTEM_H

#include <cstddef>
#include <memory>
#include <unordered_map>

#include "asset/asset.h"
#include "asset/asset_load_request.h"
#include "async/async_queue.h"
#include "base/type.h"
#include "render_camera.h"

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
    // One resolved, processed asset held by the render cache. `payload` pins the
    // loaded resource so it can't be unloaded while the renderer still needs it.
    struct RenderCacheEntry
    {
        asset::AssetID asset_id;
        asset::AssetPayload payload;
    };

    // The render-module facade. Reconstruction re-owns the RHI backend, the
    // resource-queue drain + render cache, and the scene graph here
    // (docs/render/render_module.md). Today it consumes the async load queue in
    // two modes: a full bootstrap drain at init, and a budgeted drain per frame.
    class RenderSystem
    {
    public:
        RenderSystem();
        ~RenderSystem();

        // Runtime pass: drain a bounded number of requests so no frame stalls.
        void Tick(float delta_time);

        RenderCamera *GetRenderCamera() { return render_camera_.get(); }

        bool IsReady(asset::RequestID request_id) const;
        const RenderCacheEntry *GetCached(asset::RequestID request_id) const;

    private:
        // Lifecycle belongs to RuntimeContext: it owns this object and is the only
        // caller allowed to bring it up, so no one else can re-initialize it.
        friend class runtime::RuntimeContext;

        // Owns the resource pipeline and takes the load queue it drains.
        void Initialize(GraphicsAPIType api_type, async::AsyncQueue<asset::AssetLoadRequest> *load_queue);
        // Bootstrap pass: drain every queued request now (blocking at init is fine).
        void PostInitialize();

        // Drain up to max_items requests (0 = unlimited, the bootstrap pass).
        void ConsumeRequests(std::size_t max_items);
        bool ConsumeOne(const asset::AssetLoadRequest &request, RenderCacheEntry &entry);

    private:
        std::unique_ptr<RenderCamera> render_camera_;
        std::unique_ptr<resource::ResourcePipeline> resource_pipeline_;
        async::AsyncQueue<asset::AssetLoadRequest> *load_queue_ = nullptr;

        std::unordered_map<asset::RequestID, RenderCacheEntry> render_cache_;
    };
}

#endif
