#include "render_system.h"

#include "asset/asset_manager.h"
#include "asset/audio.h"
#include "asset/mesh.h"
#include "asset/model.h"
#include "asset/shader.h"
#include "asset/shader_program.h"
#include "asset/texture.h"
#include "log/logger.h"
#include "resource/resource_pipeline.h"

namespace kpengine::render
{
    namespace
    {
        // Runtime frame budget: cap the per-frame load+compile work so a burst of
        // requests never stalls a frame. 0 means "no budget" (the bootstrap pass).
        constexpr std::size_t kMaxRuntimeLoadsPerFrame = 2;
    }

    RenderSystem::RenderSystem() : render_camera_(std::make_unique<RenderCamera>())
    {
    }

    RenderSystem::~RenderSystem() = default;

    void RenderSystem::Initialize(GraphicsAPIType api_type, async::AsyncQueue<asset::AssetLoadRequest> *load_queue)
    {
        load_queue_ = load_queue;

        resource_pipeline_ = std::make_unique<resource::ResourcePipeline>();
        resource::ResourcePipelineContext context;
        context.graphics_type = api_type;
        resource_pipeline_->Initialize(context);
    }

    void RenderSystem::PostInitialize()
    {
        // Bootstrap: load + process everything already queued, before the main loop.
        ConsumeRequests(0);
    }

    void RenderSystem::Tick(float delta_time)
    {
        (void)delta_time;
        ConsumeRequests(kMaxRuntimeLoadsPerFrame);
    }

    bool RenderSystem::IsReady(asset::RequestID request_id) const
    {
        return render_cache_.find(request_id) != render_cache_.end();
    }

    const RenderCacheEntry *RenderSystem::GetCached(asset::RequestID request_id) const
    {
        auto it = render_cache_.find(request_id);
        return it == render_cache_.end() ? nullptr : &it->second;
    }

    void RenderSystem::ConsumeRequests(std::size_t max_items)
    {
        if (!load_queue_)
        {
            return;
        }

        std::size_t consumed = 0;
        asset::AssetLoadRequest request;
        while (load_queue_->TryPop(request))
        {
            if (max_items != 0 && consumed >= max_items)
            {
                break;
            }

            RenderCacheEntry entry;
            if (ConsumeOne(request, entry))
            {
                render_cache_[request.request_id] = std::move(entry);
            }
            ++consumed;
        }
    }

    bool RenderSystem::ConsumeOne(const asset::AssetLoadRequest &request, RenderCacheEntry &entry)
    {
        auto &asset_manager = asset::AssetManager::GetInstance();
        const asset::AssetID id = asset_manager.LoadSync(request.path);
        if (!id.IsValid())
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR, "Failed to load asset: %s", request.path.c_str());
            return false;
        }

        entry.asset_id = id;

        // Bake shader programs through the resource pipeline, then pin the payload
        // per type so it can't be unloaded while the renderer still holds it.
        switch (request.type)
        {
        case asset::AssetType::KPAT_ShaderProgram:
        {
            auto program = asset_manager.GetResource<asset::ShaderProgramResource>(id);
            if (!program)
            {
                return false;
            }
            resource_pipeline_->ProcessShader(program->GatherShaders());
            entry.payload = std::move(program);
            return true;
        }
        case asset::AssetType::KPAT_Shader:
            entry.payload = asset_manager.GetResource<asset::ShaderResource>(id);
            return true;
        case asset::AssetType::KPAT_Texture:
            entry.payload = asset_manager.GetResource<asset::TextureResource>(id);
            return true;
        case asset::AssetType::KPAT_Mesh:
            entry.payload = asset_manager.GetResource<asset::MeshResource>(id);
            return true;
        case asset::AssetType::KPAT_Model:
            entry.payload = asset_manager.GetResource<asset::ModelResource>(id);
            return true;
        case asset::AssetType::KPAT_Audio:
            entry.payload = asset_manager.GetResource<asset::AudioResource>(id);
            return true;
        default:
            return false;
        }
    }
}
