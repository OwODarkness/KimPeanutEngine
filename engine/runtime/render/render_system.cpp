#include "render_system.h"

#include <cstddef>

#include "asset/asset_manager.h"
#include "asset/mesh.h"
#include "asset/model.h"
#include "asset/shader.h"
#include "asset/shader_program.h"
#include "asset/texture.h"
#include "data/mesh.h"
#include "graphics/backend/common/render_backend.h"
#include "graphics/backend/common/sampler.h"
#include "graphics/backend/common/texture.h"
#include "log/logger.h"
#include "resource/resource_pipeline.h"
#include "resource/shader_operation.h"

namespace kpengine::render
{
    namespace
    {
        // Runtime frame budget: cap the per-frame load+compile work so a burst of
        // requests never stalls a frame. 0 means "no budget" (the bootstrap pass).
        constexpr std::size_t kMaxRuntimeLoadsPerFrame = 2;

        graphics::TextureSettings DefaultTextureSettings()
        {
            graphics::TextureSettings settings{};
            settings.mip_levels = 1;
            settings.format = TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
            settings.usage = graphics::TextureUsage::TEXTURE_USAGE_TRANSFER_DST |
                             graphics::TextureUsage::TEXTURE_USAGE_SAMPLE;
            return settings;
        }
    }

    bool BuildDefaultPipelineDesc(asset::ShaderProgramResource &program,
                                  graphics::PipelineDesc &out_desc)
    {
        const auto vert_shader = program.GetShader(ShaderStage::SHADER_STAGE_VERTEX,
                                                   ShaderFormat::SHADER_FORMAT_GLSL);
        const auto frag_shader = program.GetShader(ShaderStage::SHADER_STAGE_FRAGMENT,
                                                   ShaderFormat::SHADER_FORMAT_GLSL);
        if (!vert_shader || !frag_shader || !vert_shader->data || !frag_shader->data ||
            vert_shader->status != asset::ShaderStatus::Ready ||
            frag_shader->status != asset::ShaderStatus::Ready)
        {
            return false;
        }

        graphics::PipelineDesc desc{};
        desc.vert_shader = vert_shader->data.get();
        desc.frag_shader = frag_shader->data.get();
        desc.binding_descs = {{0, sizeof(data::Vertex), false}};
        desc.attri_descs = {
            {0, 0, graphics::VertexFormat::VERTEX_FORMAT_THREE_FLOATS,
             offsetof(data::Vertex, position)},
            {1, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS,
             offsetof(data::Vertex, tex_coord)},
        };
        desc.descriptor_binding_descs = {
            {{0, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
              ShaderStage::SHADER_STAGE_VERTEX},
             {1, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM,
              ShaderStage::SHADER_STAGE_VERTEX},
             {2, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
              ShaderStage::SHADER_STAGE_FRAGMENT}},
        };
        desc.raster_state.front_face = graphics::FrontFace::FRONT_FACE_COUNTER_CLOCKWISE;
        out_desc = std::move(desc);
        return true;
    }

    RenderSystem::RenderSystem() = default;

    RenderSystem::~RenderSystem()
    {
        Shutdown();
    }

    void RenderSystem::Initialize(const RenderSystemInitInfo &info)
    {
        if (!info.native_window || !info.resize_dispatcher || !info.load_queue)
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR, "RenderSystem initialization requires window, resize dispatcher, and load queue");
            return;
        }
        load_queue_ = info.load_queue;

        resource_pipeline_ = std::make_unique<resource::ResourcePipeline>();
        resource::ResourcePipelineContext context;
        context.graphics_type = info.api_type;
        resource_pipeline_->Initialize(context);

        backend_ = graphics::RenderBackend::CreateGraphicsBackEnd(info.api_type);
        if (!backend_)
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR, "No graphics backend for API %d",
                   static_cast<int>(info.api_type));
            return;
        }
        backend_->BindWindowResize(*info.resize_dispatcher);
        backend_->Initialize(info.native_window);

        constexpr size_t kFrameUniformCapacity = 64 * 1024;
        frame_contexts_.resize(backend_->GetFramesInFlight());
        for (FrameContext &context : frame_contexts_)
        {
            context.Initialize(*backend_, kFrameUniformCapacity);
        }
    }

    void RenderSystem::PostInitialize()
    {
        // Bootstrap: load + process everything already queued, before the main loop.
        ConsumeRequests(0);
        KP_LOG("RenderLog", LOG_LEVEL_INFO,
               "Bootstrap drained: %d distinct shader(s) loaded", GetLoadedShaderCount());
    }

    void RenderSystem::Tick(float delta_time)
    {
        ConsumeRequests(kMaxRuntimeLoadsPerFrame);
        if (!backend_)
        {
            return;
        }

        backend_->BeginFrame();
        FrameContext *frame_context = GetCurrentFrameContext();
        if (frame_context)
        {
            elapsed_seconds_ += delta_time;
            frame_context->Begin(backend_->GetCurrentFrameIndex(),
                                 {frame_number_, elapsed_seconds_, delta_time});
            // Scene scheduling will consume this context in the next Phase 3.4 slice.
            frame_context->End();
            ++frame_number_;
        }
        backend_->EndFrame();
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

    graphics::PipelineHandle RenderSystem::GetPipeline(asset::RequestID request_id) const
    {
        const RenderCacheEntry *entry = GetCached(request_id);
        const auto *pipeline = entry ? std::get_if<graphics::PipelineHandle>(&entry->resource) : nullptr;
        return pipeline ? *pipeline : graphics::PipelineHandle{};
    }

    int RenderSystem::GetLoadedShaderCount() const
    {
        return resource_pipeline_
                   ? static_cast<int>(resource_pipeline_->GetProcessedShaderCount())
                   : 0;
    }

    std::vector<asset::ShaderPtr> RenderSystem::GetCachedShaders(
        const asset::ShaderProgramResource *program) const
    {
        std::vector<asset::ShaderPtr> shaders;
        if (!program)
        {
            return shaders;
        }
        for (auto &shader : program->GatherShaders())
        {
            // Only stages whose data finished baking can back a pipeline; a failed
            // stage must never reach the RHI as part of a PipelineDesc.
            if (shader && shader->status == asset::ShaderStatus::Ready)
            {
                shaders.push_back(shader);
            }
        }
        return shaders;
    }

    void RenderSystem::ConsumeRequests(std::size_t max_items)
    {
        if (!load_queue_)
        {
            return;
        }

        std::size_t consumed = 0;
        asset::AssetLoadRequest request;
        while ((max_items == 0 || consumed < max_items) && load_queue_->TryPop(request))
        {
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
        // The render cache only holds GPU-bound artifacts. Refuse the rest (audio
        // today) before LoadSync, so a non-render request never burns a render
        // frame budget decoding a file the renderer will never read.
        switch (request.type)
        {
        case asset::AssetType::KPAT_ShaderProgram:
        case asset::AssetType::KPAT_Shader:
        case asset::AssetType::KPAT_Texture:
        case asset::AssetType::KPAT_Mesh:
        case asset::AssetType::KPAT_Model:
            break;
        default:
            KP_LOG("RenderLog", LOG_LEVEL_WARNING,
                   "Skipping non-render asset request (type %d): %s",
                   static_cast<int>(request.type), request.path.c_str());
            return false;
        }

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
            // Startup shader compile is a slow, one-time pass; report it so the
            // log (and later a loading screen) can show progress. Observer fires
            // per stage before it is processed; done = already finished.
            resource_pipeline_->ProcessShader(
                program->GatherShaders(),
                [](resource::ShaderProcessPhase phase, int done, int total,
                   const asset::ShaderResource *shader) {
                    KP_LOG("RenderLog", LOG_LEVEL_INFO,
                           "Shader %d/%d: %s (phase %d)",
                           done + 1, total, shader->desc.file.c_str(),
                           static_cast<int>(phase));
                });
            const graphics::PipelineHandle pipeline = GetOrCreateDefaultPipeline(id, *program);
            if (!pipeline.IsValid())
            {
                KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                       "No default pipeline created for shader program: %s",
                       request.path.c_str());
                return false;
            }
            entry.resource = pipeline;
            entry.payload = std::move(program);
            return true;
        }
        case asset::AssetType::KPAT_Shader:
            entry.payload = asset_manager.GetResource<asset::ShaderResource>(id);
            return true;
        case asset::AssetType::KPAT_Texture:
        {
            auto texture = asset_manager.GetResource<asset::TextureResource>(id);
            if (!texture || !texture->data)
            {
                return false;
            }
            const graphics::TextureHandle texture_handle = GetOrCreateTexture(id, *texture->data);
            const graphics::SamplerHandle sampler_handle = GetOrCreateDefaultSampler();
            entry.payload = std::move(texture);
            if (!texture_handle.IsValid() || !sampler_handle.IsValid())
            {
                return false;
            }
            entry.resource = TextureBinding{texture_handle, sampler_handle};
            return true;
        }
        case asset::AssetType::KPAT_Mesh:
        {
            auto mesh = asset_manager.GetResource<asset::MeshResource>(id);
            if (!mesh || !mesh->data)
            {
                return false;
            }
            const graphics::MeshHandle mesh_handle = GetOrCreateMesh(id, *mesh->data);
            entry.payload = std::move(mesh);
            if (!mesh_handle.IsValid())
            {
                return false;
            }
            entry.resource = mesh_handle;
            return true;
        }
        case asset::AssetType::KPAT_Model:
        {
            auto model = asset_manager.GetResource<asset::ModelResource>(id);
            if (!model)
            {
                return false;
            }
            const asset::AssetID mesh_id = model->GetData(asset::ModelGeometryType::KPMG_Mesh);
            auto mesh = model->GetMesh();
            if (!mesh || !mesh->data)
            {
                return false;
            }
            const graphics::MeshHandle mesh_handle = GetOrCreateMesh(mesh_id, *mesh->data);
            entry.payload = std::move(model);
            if (!mesh_handle.IsValid())
            {
                return false;
            }
            entry.resource = mesh_handle;
            return true;
        }
        default:
            return false;
        }
    }

    graphics::PipelineHandle RenderSystem::GetOrCreateDefaultPipeline(
        asset::AssetID program_id, asset::ShaderProgramResource &program)
    {
        const uint64_t key = program_id.Pack();
        const auto existing = pipeline_cache_.find(key);
        if (existing != pipeline_cache_.end())
        {
            return existing->second;
        }

        if (!backend_)
        {
            return {};
        }

        graphics::PipelineDesc desc{};
        if (!BuildDefaultPipelineDesc(program, desc))
        {
            return {};
        }

        const graphics::PipelineHandle handle = backend_->CreatePipelineResource(desc);
        if (handle.IsValid())
        {
            pipeline_cache_.emplace(key, handle);
        }
        return handle;
    }

    graphics::MeshHandle RenderSystem::GetOrCreateMesh(asset::AssetID asset_id,
                                                        const data::MeshData &data)
    {
        const uint64_t key = asset_id.Pack();
        const auto existing = mesh_cache_.find(key);
        if (existing != mesh_cache_.end())
        {
            return existing->second;
        }
        if (!backend_)
        {
            return {};
        }

        const graphics::MeshHandle handle = backend_->CreateMesh(data);
        if (handle.IsValid())
        {
            mesh_cache_.emplace(key, handle);
        }
        return handle;
    }

    graphics::TextureHandle RenderSystem::GetOrCreateTexture(asset::AssetID asset_id,
                                                              const data::TextureData &data)
    {
        const uint64_t key = asset_id.Pack();
        const auto existing = texture_cache_.find(key);
        if (existing != texture_cache_.end())
        {
            return existing->second;
        }
        if (!backend_)
        {
            return {};
        }

        graphics::TextureSettings settings = DefaultTextureSettings();
        settings.format = data.format;
        const graphics::TextureHandle handle = backend_->CreateTexture(data, settings);
        if (handle.IsValid())
        {
            texture_cache_.emplace(key, handle);
        }
        return handle;
    }

    graphics::SamplerHandle RenderSystem::GetOrCreateDefaultSampler()
    {
        if (default_sampler_handle_.IsValid())
        {
            return default_sampler_handle_;
        }
        if (!backend_)
        {
            return {};
        }

        graphics::SamplerSettings settings{};
        settings.max_anisotropy = 16.f;
        default_sampler_handle_ = backend_->CreateSampler(settings);
        return default_sampler_handle_;
    }

    FrameContext *RenderSystem::GetCurrentFrameContext()
    {
        if (!backend_ || !backend_->GetCommandRecorder())
        {
            return nullptr;
        }
        const uint32_t index = backend_->GetCurrentFrameIndex();
        return index < frame_contexts_.size() ? &frame_contexts_[index] : nullptr;
    }

    void RenderSystem::Shutdown()
    {
        if (!backend_)
        {
            return;
        }

        for (FrameContext &context : frame_contexts_)
        {
            context.Cleanup();
        }
        frame_contexts_.clear();
        for (const auto &[key, handle] : mesh_cache_)
        {
            (void)key;
            backend_->DestroyMesh(handle);
        }
        mesh_cache_.clear();
        for (const auto &[key, handle] : texture_cache_)
        {
            (void)key;
            backend_->DestroyTexture(handle);
        }
        texture_cache_.clear();
        if (default_sampler_handle_.IsValid())
        {
            backend_->DestroySampler(default_sampler_handle_);
            default_sampler_handle_ = {};
        }
        for (const auto &[key, handle] : pipeline_cache_)
        {
            (void)key;
            backend_->DestroyPipelineResource(handle);
        }
        pipeline_cache_.clear();
        backend_->Cleanup();
        backend_.reset();
    }
}
