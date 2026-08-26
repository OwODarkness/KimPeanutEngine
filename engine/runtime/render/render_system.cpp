#include "render_system.h"

#include <algorithm>

#include "asset/asset_manager.h"
#include "asset/mesh.h"
#include "asset/model.h"
#include "asset/shader.h"
#include "asset/shader_program.h"
#include "asset/texture.h"
#include "graphics/backend/common/render_backend.h"
#include "graphics/backend/common/command_recorder.h"
#include "log/logger.h"
#include "render/material/material_system.h"
#include "render/render_world/scene_draw_list.h"
#include "render/render_world/scene_visibility.h"
#include "render_resource_resolver.h"
#include "resource/resource_pipeline.h"
#include "resource/shader_operation.h"

namespace kpengine::render
{
    namespace
    {
        constexpr const char *GetGraphicsApiName(GraphicsAPIType api_type)
        {
            switch (api_type)
            {
            case GraphicsAPIType::GRAPHICS_API_OPENGL:
                return "OpenGL";
            case GraphicsAPIType::GRAPHICS_API_VULKAN:
                return "Vulkan";
            case GraphicsAPIType::GRAPHICS_API_UNKNOW:
            default:
                return "Unknown";
            }
        }

        // Runtime frame budget: cap the per-frame load+compile work so a burst of
        // requests never stalls a frame. 0 means "no budget" (the bootstrap pass).
        constexpr std::size_t kMaxRuntimeLoadsPerFrame = 2;
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
        bootstrap_scene_info_ = info.bootstrap_scene;
        material_system_ = std::make_unique<MaterialSystem>();

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
        KP_LOG("RenderLog", LOG_LEVEL_INFO, "RenderSystem selected %s graphics backend",
               GetGraphicsApiName(info.api_type));
        backend_->BindWindowResize(*info.resize_dispatcher);
        backend_->Initialize(info.native_window);


        resource_resolver_ =
            std::make_unique<RenderResourceResolver>(*backend_, *resource_pipeline_);
        material_system_->SetResourceResolver(resource_resolver_.get());

        constexpr size_t kFrameUniformCapacity = 64 * 1024;
        frame_contexts_.resize(backend_->GetFramesInFlight());
        for (FrameContext &context : frame_contexts_)
        {
            context.Initialize(*backend_, kFrameUniformCapacity);
        }

        const graphics::Extent2D extent = backend_->GetRenderExtent();
        scene_render_target_.Initialize(*backend_, extent.width, extent.height);
        if (!scene_render_target_.IsValid())
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR, "Failed to create scene render target");
        }
        ConfigurePassSchedule();
    }

    void RenderSystem::PostInitialize()
    {
        // Bootstrap: load + process everything already queued, before the main loop.
        ConsumeRequests(0);
        CreateBootstrapScene();
        KP_LOG("RenderLog", LOG_LEVEL_INFO,
               "Bootstrap drained: %d distinct shader(s) loaded", GetLoadedShaderCount());
    }

    void RenderSystem::Tick(float delta_time)
    {
        BeginFrame(delta_time);
        EndFrame();
    }

    void RenderSystem::BeginFrame(float delta_time)
    {
        ConsumeRequests(kMaxRuntimeLoadsPerFrame);
        material_system_->RefreshResources();
        render_world_.ApplyPendingCommands();
        if (!backend_)
        {
            return;
        }

        ApplyPendingSceneRenderTargetExtent();
        backend_->BeginFrame();
        active_frame_context_ = GetCurrentFrameContext();
        if (active_frame_context_)
        {
            elapsed_seconds_ += delta_time;
            active_frame_context_->Begin(backend_->GetCurrentFrameIndex(),
                                         {frame_number_, elapsed_seconds_, delta_time},
                                         {scene_render_target_.GetWidth(),
                                          scene_render_target_.GetHeight()});
            editor_composite_recorded_ = false;
            RecordScenePass();
        }
    }

    void RenderSystem::EndFrame()
    {
        if (!backend_)
        {
            return;
        }
        if (active_frame_context_)
        {
            active_frame_context_->End();
            active_frame_context_ = nullptr;
            ++frame_number_;
        }
        backend_->EndFrame();
    }

    bool RenderSystem::ExecuteEditorCompositePass(const std::function<void()> &record_pass)
    {
        if (!active_frame_context_ || editor_composite_recorded_ || !record_pass ||
            !pass_schedule_.IsValid())
        {
            return false;
        }
        record_pass();
        editor_composite_recorded_ = true;
        return true;
    }

    void RenderSystem::RequestSceneRenderTargetExtent(uint32_t width, uint32_t height)
    {
        if (width == 0 || height == 0)
        {
            return;
        }
        pending_scene_render_target_extent_ = {width, height};
    }

    GraphicsContext RenderSystem::GetGraphicsContext()
    {
        return backend_ ? backend_->GetGraphicsContext()
                        : GraphicsContext{GraphicsAPIType::GRAPHICS_API_UNKNOW, nullptr};
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

    void RenderSystem::ConfigurePassSchedule()
    {
        const bool added_scene = pass_schedule_.AddPass(
            {"ScenePass", {{RenderPassResource::SceneColor, RenderPassAccess::Write}}, false});
        const bool added_editor = pass_schedule_.AddPass(
            {"EditorCompositePass", {{RenderPassResource::SceneColor, RenderPassAccess::Read}}, true});
        std::string error;
        if (!added_scene || !added_editor || !pass_schedule_.Validate(error))
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR, "Invalid render pass schedule: %s", error.c_str());
        }
    }

    void RenderSystem::RecordScenePass()
    {
        if (!active_frame_context_ || !pass_schedule_.IsValid())
        {
            return;
        }

        graphics::CommandRecorder *const recorder = backend_->GetCommandRecorder();
        if (!recorder || !scene_render_target_.BeginRecording(*recorder))
        {
            return;
        }
        const graphics::Extent2D extent = active_frame_context_->GetRenderExtent();
        if (extent.height != 0)
        {
            scene_camera_.SetAspect(static_cast<float>(extent.width) / static_cast<float>(extent.height));
            const CameraData camera_data = scene_camera_.GetCameraData();
            graphics::PerPassData per_pass_data{};
            per_pass_data.camera_data.view = camera_data.view;
            per_pass_data.camera_data.proj = camera_data.proj;
            const std::vector<MeshProxy> visible_proxies = SceneVisibility::BuildVisibleProxies(
                scene_camera_.GetViewProjectionMatrix(), render_world_.Snapshot());
            const SceneDrawLists draw_lists = SceneDrawListBuilder::Build(
                visible_proxies, *material_system_, *resource_resolver_);
            for (const SceneDrawItem &item : draw_lists.opaque)
            {
                RecordMeshProxy(item.proxy, per_pass_data, *recorder);
            }
            for (const SceneDrawItem &item : draw_lists.alpha_blend)
            {
                RecordMeshProxy(item.proxy, per_pass_data, *recorder);
            }
        }
        scene_render_target_.EndRecording(*recorder);
    }

    void RenderSystem::RecordMeshProxy(const MeshProxy &proxy,
                                       const graphics::PerPassData &per_pass_data,
                                       graphics::CommandRecorder &recorder)
    {
        if (!proxy.flags.visible || !proxy.mesh.IsValid())
        {
            return;
        }

        graphics::PerObjectData per_object_data{};
        per_object_data.model = Matrix4f::MakeTransformMatrix(proxy.world_transform).Transpose();
        const UniformAllocation per_pass = active_frame_context_->AllocateUniform(per_pass_data);
        const UniformAllocation per_object = active_frame_context_->AllocateUniform(per_object_data);
        if (!per_pass.IsValid() || !per_object.IsValid())
        {
            return;
        }

        const std::vector<graphics::ResourceBinding> draw_bindings{
            graphics::UniformBufferBinding{0, 0, per_pass.buffer, per_pass.offset, per_pass.range},
            graphics::UniformBufferBinding{0, 1, per_object.buffer, per_object.offset, per_object.range},
        };
        const FrameMaterialBinding material_binding = active_frame_context_->CreateMaterialBinding(
            *material_system_, *resource_resolver_, proxy.material, draw_bindings);
        if (!active_frame_context_->IsMaterialBindingCurrent(material_binding))
        {
            return;
        }

        recorder.BindPipeline(material_binding.pipeline);
        recorder.BindMesh(proxy.mesh);
        recorder.BindResourceBindings(material_binding.pipeline, material_binding.descriptor_set);
        recorder.DrawIndexed();
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
                   const asset::ShaderResource *shader)
                {
                    KP_LOG("RenderLog", LOG_LEVEL_INFO,
                           "Shader %d/%d: %s (phase %d)",
                           done + 1, total, shader->desc.file.c_str(),
                           static_cast<int>(phase));
                });
            const graphics::PipelineHandle pipeline =
                resource_resolver_->GetOrCreateDefaultPipeline(id, *program);
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
            const TextureBinding texture_binding =
                resource_resolver_->GetOrCreateTextureBinding(id, *texture->data);
            entry.payload = std::move(texture);
            if (!texture_binding.texture.IsValid() || !texture_binding.sampler.IsValid())
            {
                return false;
            }
            entry.resource = texture_binding;
            return true;
        }
        case asset::AssetType::KPAT_Mesh:
        {
            auto mesh = asset_manager.GetResource<asset::MeshResource>(id);
            if (!mesh || !mesh->data)
            {
                return false;
            }
            const graphics::MeshHandle mesh_handle = resource_resolver_->GetOrCreateMesh(id, *mesh->data);
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
            const graphics::MeshHandle mesh_handle =
                resource_resolver_->GetOrCreateMesh(mesh_id, *mesh->data);
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

    const RenderCacheEntry *RenderSystem::FindCached(asset::AssetID asset_id) const
    {
        for (const auto &[request_id, entry] : render_cache_)
        {
            (void)request_id;
            if (entry.asset_id == asset_id)
            {
                return &entry;
            }
        }
        return nullptr;
    }

    MaterialInstanceHandle RenderSystem::CreateDefaultTexturedMaterial(
        asset::AssetID shader_program, asset::AssetID texture_asset)
    {
        if (!material_system_ || !shader_program.IsValid() || !texture_asset.IsValid())
        {
            return {};
        }

        MaterialTemplateDesc desc{};
        desc.shader_program = shader_program;
        desc.shading_model = MaterialShadingModel::Unlit;
        desc.parameters = {
            {"base_color", Vector4f{1.0f, 1.0f, 1.0f, 1.0f}},
            {"base_color_texture", MaterialTextureSamplerValue{texture_asset, {}}, 2},
        };
        bootstrap_material_template_ = material_system_->CreateTemplate(desc);
        if (!bootstrap_material_template_.IsValid())
        {
            return {};
        }
        return material_system_->CreateInstance({bootstrap_material_template_, {}});
    }

    void RenderSystem::CreateBootstrapScene()
    {
        if (!bootstrap_scene_info_.IsComplete() || bootstrap_renderable_.IsValid())
        {
            return;
        }

        auto &asset_manager = asset::AssetManager::GetInstance();
        const asset::AssetID pipeline_asset =
            asset_manager.LoadSync(bootstrap_scene_info_.shader_program_path);
        const asset::AssetID model_asset = asset_manager.LoadSync(bootstrap_scene_info_.model_path);
        const asset::AssetID texture_asset = asset_manager.LoadSync(bootstrap_scene_info_.texture_path);
        const RenderCacheEntry *model_entry = FindCached(model_asset);
        const auto *mesh = model_entry
                               ? std::get_if<graphics::MeshHandle>(&model_entry->resource)
                               : nullptr;
        bootstrap_material_instance_ = CreateDefaultTexturedMaterial(pipeline_asset, texture_asset);
        if (!mesh || !bootstrap_material_instance_.IsValid())
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                   "Bootstrap scene could not create a mesh or material instance");
            return;
        }

        MeshProxyDesc proxy_desc{};
        proxy_desc.mesh = *mesh;
        proxy_desc.material = bootstrap_material_instance_;
        proxy_desc.world_transform.scale_ = {0.5f, 0.5f, 0.5f};
        bootstrap_renderable_ = render_world_.EnqueueCreate(proxy_desc);
        KP_LOG("RenderLog", LOG_LEVEL_INFO, "Bootstrap mesh proxy registered");
    }

    void RenderSystem::ApplyPendingSceneRenderTargetExtent()
    {
        if (!backend_ || pending_scene_render_target_extent_.width == 0 ||
            pending_scene_render_target_extent_.height == 0)
        {
            return;
        }

        const graphics::Extent2D requested = pending_scene_render_target_extent_;
        pending_scene_render_target_extent_ = {};
        if (requested.width == scene_render_target_.GetWidth() &&
            requested.height == scene_render_target_.GetHeight())
        {
            return;
        }

        // A target is shared by all backend frame slots. Resizing after only the
        // current slot's fence could destroy an attachment used by another slot.
        // This rare boundary waits once, before the next BeginFrame records work.
        backend_->WaitIdle();
        active_frame_context_ = nullptr;
        scene_render_target_.Initialize(*backend_, requested.width, requested.height);
        if (!scene_render_target_.IsValid())
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                   "Failed to resize scene render target to %u x %u", requested.width,
                   requested.height);
        }
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
            material_system_.reset();
            resource_resolver_.reset();
            return;
        }

        // Render-owned pipelines, descriptors, buffers, and targets may still be
        // referenced by an earlier frame slot. Retire all submitted work before
        // releasing any of those shared GPU objects.
        backend_->WaitIdle();
        render_world_.Clear();
        bootstrap_renderable_ = {};
        if (bootstrap_material_instance_.IsValid())
        {
            material_system_->DestroyInstance(bootstrap_material_instance_);
            bootstrap_material_instance_ = {};
        }
        if (bootstrap_material_template_.IsValid())
        {
            material_system_->DestroyTemplate(bootstrap_material_template_);
            bootstrap_material_template_ = {};
        }
        material_system_.reset();
        for (FrameContext &context : frame_contexts_)
        {
            context.Cleanup();
        }
        frame_contexts_.clear();
        scene_render_target_.Cleanup();
        resource_resolver_->Cleanup();
        resource_resolver_.reset();
        backend_->Cleanup();
        backend_.reset();
    }
}
