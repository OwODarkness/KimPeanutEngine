#include "render_system.h"

#include <stdexcept>
#include <utility>

#include "asset/asset_manager.h"
#include "asset/mesh.h"
#include "asset/model.h"
#include "asset/shader.h"
#include "asset/shader_program.h"
#include "asset/texture.h"
#include "graphics/backend/common/render_backend.h"
#include "log/logger.h"
#include "render/material/material_system.h"
#include "render/material/material_asset_resolver.h"
#include "render/render_capture_service_internal.h"
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

    RenderSystemInitResult RenderSystem::Initialize(const RenderSystemInitInfo &info)
    {
        if (lifecycle_state_ != RenderSystemLifecycleState::Uninitialized)
        {
            last_diagnostic_ = "RenderSystem can only be initialized once.";
            return {false, last_diagnostic_};
        }
        if (!info.native_window || !info.resize_dispatcher || !info.load_queue)
        {
            last_diagnostic_ =
                "RenderSystem initialization requires window, resize dispatcher, and load queue.";
            KP_LOG("RenderLog", LOG_LEVEL_ERROR, "%s", last_diagnostic_.c_str());
            return {false, last_diagnostic_};
        }
        try
        {
            load_queue_ = info.load_queue;
            material_system_ = std::make_unique<MaterialSystem>();
            material_asset_resolver_ = std::make_unique<MaterialAssetResolver>(*material_system_);

            resource_pipeline_ = std::make_unique<resource::ResourcePipeline>();
            resource::ResourcePipelineContext context;
            context.graphics_type = info.api_type;
            resource_pipeline_->Initialize(context);

            RenderBackendFactory factory = info.backend_factory;
            if (!factory)
            {
                factory = [](GraphicsAPIType api_type)
                { return graphics::RenderBackend::CreateGraphicsBackEnd(api_type); };
            }
            backend_ = factory(info.api_type);
            if (!backend_)
            {
                throw std::runtime_error("No graphics backend is available for the requested API.");
            }
            KP_LOG("RenderLog", LOG_LEVEL_INFO, "RenderSystem selected %s graphics backend",
                   GetGraphicsApiName(info.api_type));
            backend_->BindWindowResize(*info.resize_dispatcher);
            backend_->Initialize(info.native_window);
            backend_initialized_ = true;

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
            deferred_renderer_ = std::make_unique<DeferredRenderer>();
            const DeferredRendererInitResult renderer_result = deferred_renderer_->Initialize(
                {*backend_, *resource_pipeline_, *resource_resolver_, *material_system_},
                extent.width, extent.height);
            if (!renderer_result)
            {
                throw std::runtime_error(renderer_result.diagnostic);
            }
            render_capture_service_ = std::make_unique<RenderCaptureService>(
                backend_->GetRenderTargetReadback(),
                [this](CaptureView view)
                {
                    return deferred_renderer_ ? deferred_renderer_->GetCaptureTarget(view)
                                               : graphics::RenderTargetHandle{};
                },
                [this] { return frame_number_; });
            // Frame the ~165-unit rock bootstrap fixture: pull the camera back and
            // extend the far plane (the old default far=10 culled it entirely).
            ApplyDefaultCamera();
            lifecycle_state_ = RenderSystemLifecycleState::Ready;
            last_diagnostic_.clear();
            return {true, {}};
        }
        catch (const std::exception &error)
        {
            last_diagnostic_ = error.what();
        }
        catch (...)
        {
            last_diagnostic_ = "Unknown exception during RenderSystem initialization.";
        }
        KP_LOG("RenderLog", LOG_LEVEL_ERROR, "RenderSystem initialization failed: %s",
               last_diagnostic_.c_str());
        CleanupOwnedState();
        lifecycle_state_ = RenderSystemLifecycleState::Uninitialized;
        return {false, last_diagnostic_};
    }

    bool RenderSystem::PostInitialize()
    {
        if (!IsState(RenderSystemLifecycleState::Ready))
        {
            return false;
        }
        // Drain any generic requests already queued before the first frame and
        // warm Render-owned built-ins before authored materials are resolved.
        ConsumeRequests(0);
        if (material_asset_resolver_ == nullptr ||
            !material_asset_resolver_->WarmBuiltInTextures())
        {
            last_diagnostic_ = "Render built-in material textures could not be warmed";
            KP_LOG("RenderLog", LOG_LEVEL_ERROR, "%s", last_diagnostic_.c_str());
            return false;
        }
        KP_LOG("RenderLog", LOG_LEVEL_INFO,
               "Startup asset drain: %d distinct shader(s) loaded", GetLoadedShaderCount());
        return true;
    }

    bool RenderSystem::Tick(float delta_time)
    {
        if (!BeginFrame(delta_time))
        {
            return false;
        }
        return EndFrame();
    }

    bool RenderSystem::BeginFrame(float delta_time)
    {
        if (!IsState(RenderSystemLifecycleState::Ready))
        {
            return false;
        }
        ConsumeRequests(kMaxRuntimeLoadsPerFrame);
        material_system_->RefreshResources();
        DrainRenderableSources();
        render_world_.ApplyPendingCommands();
        DrainLightSources();
        DrainCameraSources();
        DrainEnvironmentSources();
        const std::vector<Light> light_snapshot = light_world_.Snapshot();
        if (!deferred_renderer_)
        {
            return false;
        }
        deferred_renderer_->ApplyPendingExtent();
        backend_->BeginFrame();
        active_frame_context_ = GetCurrentFrameContext();
        lifecycle_state_ = RenderSystemLifecycleState::FrameActive;
        if (active_frame_context_)
        {
            elapsed_seconds_ += delta_time;
            active_frame_context_->Begin(
                backend_->GetCurrentFrameIndex(),
                {frame_number_, elapsed_seconds_, delta_time},
                {deferred_renderer_->GetSceneRenderTarget().GetWidth(),
                 deferred_renderer_->GetSceneRenderTarget().GetHeight()});
            DeferredRendererFrameInput input{
                render_world_, light_snapshot, scene_camera_,
                environment_source_registry_.GetActiveSource(),
                environment_source_registry_.GetActiveHandle(),
                [this](ShadowHandle handle) {
                    return light_source_registry_.IsShadowHandleValid(handle);
                },
                render_capture_service_ ? render_capture_service_->GetPendingView()
                                        : std::nullopt};
            const DeferredRendererFrameResult result =
                deferred_renderer_->RecordFrame(*active_frame_context_, input);
            if (input.pending_capture.has_value())
            {
                if (!result.capture_target_ready)
                {
                    render_capture_service_->RejectPendingCapture(
                        "Render could not record the requested capture-view conversion pass");
                }
                else
                {
                    render_capture_service_->EnqueuePendingReadback();
                }
            }
        }
        return true;
    }

    bool RenderSystem::EndFrame()
    {
        if (!IsState(RenderSystemLifecycleState::FrameActive))
        {
            return false;
        }
        if (active_frame_context_)
        {
            if (deferred_renderer_)
            {
                deferred_renderer_->FinalizeFrame();
            }
            active_frame_context_->End();
            active_frame_context_ = nullptr;
            ++frame_number_;
        }
        backend_->EndFrame();
        lifecycle_state_ = RenderSystemLifecycleState::Ready;
        return true;
    }

    bool RenderSystem::ExecuteEditorCompositePass(const std::function<void()> &record_pass)
    {
        if (!IsState(RenderSystemLifecycleState::FrameActive) || !active_frame_context_ ||
            !record_pass || !deferred_renderer_)
        {
            return false;
        }
        return deferred_renderer_->ExecuteEditorCompositePass(record_pass);
    }

    void RenderSystem::RequestSceneRenderTargetExtent(uint32_t width, uint32_t height)
    {
        if (lifecycle_state_ == RenderSystemLifecycleState::Uninitialized ||
            lifecycle_state_ == RenderSystemLifecycleState::ShutDown || width == 0 || height == 0)
        {
            return;
        }
        if (deferred_renderer_)
        {
            deferred_renderer_->RequestExtent(width, height);
        }
    }

    const RenderTarget &RenderSystem::GetSceneRenderTarget() const
    {
        static const RenderTarget empty_target;
        return deferred_renderer_ ? deferred_renderer_->GetSceneRenderTarget() : empty_target;
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

    IRenderCaptureService *RenderSystem::GetRenderCaptureService()
    {
        return render_capture_service_.get();
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
        case asset::AssetType::KPAT_Material:
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
            const auto shaders = program->GatherShaders(asset::ShaderProgramVariant::Bound);
            resource_pipeline_->ProcessShader(
                shaders,
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
            MaterialSamplerDesc panorama_sampler{};
            panorama_sampler.address_v = MaterialSamplerAddressMode::ClampToEdge;
            panorama_sampler.address_w = MaterialSamplerAddressMode::ClampToEdge;
            const TextureBinding texture_binding =
                resource_resolver_->GetOrCreateTextureBinding(
                    id, *texture->data, MaterialTextureColorSpace::Srgb,
                    &panorama_sampler);
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
        case asset::AssetType::KPAT_Material:
            entry.payload = asset_manager.GetResource<asset::MaterialResource>(id);
            return std::get<asset::MaterialPtr>(entry.payload) != nullptr;
        default:
            return false;
        }
    }

    RenderableSourceResolution RenderSystem::ResolveRenderableSource(
        const PrimitiveRenderableSourceDesc &source)
    {
        const auto *const static_mesh = std::get_if<StaticMeshRenderableSourceDesc>(&source);
        if (static_mesh == nullptr)
        {
            return {RenderableSourceState::Failed, "unsupported renderable source variant", std::nullopt};
        }
        if (!static_mesh->mesh_asset.IsValid() ||
            static_mesh->mesh_asset.type != asset::AssetType::KPAT_Mesh)
        {
            return {RenderableSourceState::Failed, "static mesh source has an invalid mesh asset", std::nullopt};
        }
        MaterialInstanceHandle material_instance;
        const MaterialResolution material_resolution =
            ResolveMaterialAsset(static_mesh->material_asset, material_instance);
        if (material_resolution.state == MaterialResourceState::Failed)
        {
            return {RenderableSourceState::Failed, material_resolution.diagnostic, std::nullopt};
        }
        if (material_resolution.state != MaterialResourceState::Ready)
        {
            return {RenderableSourceState::Pending, material_resolution.diagnostic, std::nullopt};
        }

        const auto mesh = asset::AssetManager::GetInstance().GetResource<asset::MeshResource>(
            static_mesh->mesh_asset);
        if (!mesh || !mesh->data)
        {
            return {RenderableSourceState::Pending, "mesh asset is not loaded", std::nullopt};
        }
        const graphics::MeshHandle mesh_handle =
            resource_resolver_->GetOrCreateMesh(static_mesh->mesh_asset, *mesh->data);
        if (!mesh_handle.IsValid())
        {
            return {RenderableSourceState::Failed, "mesh resource creation failed", std::nullopt};
        }

        MeshProxyDesc proxy_desc{};
        proxy_desc.mesh = mesh_handle;
        proxy_desc.material = material_instance;
        proxy_desc.world_transform = static_mesh->world_transform;
        proxy_desc.world_bounds = static_mesh->world_bounds;
        proxy_desc.flags = static_mesh->flags;
        proxy_desc.lod_bias = static_mesh->lod_bias;
        return {RenderableSourceState::Ready, {}, proxy_desc};
    }

    MaterialResolution RenderSystem::ResolveMaterialAsset(asset::AssetID material_asset,
                                                           MaterialInstanceHandle &out_instance)
    {
        if (!material_asset_resolver_)
        {
            return {MaterialResourceState::Pending, "material system is not initialized"};
        }
        return material_asset_resolver_->Resolve(material_asset, out_instance);
    }

    void RenderSystem::DrainRenderableSources()
    {
        auto callback = [this](const PrimitiveRenderableSourceDesc &source)
            { return ResolveRenderableSource(source); };
        source_registry_.Drain(
            render_world_, callback);
    }

    void RenderSystem::DrainLightSources()
    {
        light_source_registry_.Drain(light_world_);
    }

    void RenderSystem::DrainCameraSources()
    {
        camera_source_registry_.Drain();
        const std::optional<CameraSourceDesc> active_source =
            camera_source_registry_.GetActiveSource();
        if (!active_source.has_value())
        {
            ApplyDefaultCamera();
            return;
        }

        const CameraSourceDesc &source = *active_source;
        scene_camera_.SetPosition(source.world_transform.position_);
        scene_camera_.SetRotation(source.world_transform.rotator_);
        scene_camera_.SetProjectionMode(source.projection_mode);
        scene_camera_.SetFOV(source.field_of_view_degrees);
        scene_camera_.SetNearPlane(source.near_plane);
        scene_camera_.SetFarPlane(source.far_plane);
        scene_camera_.SetOrthographicHeight(source.orthographic_height);
    }

    void RenderSystem::DrainEnvironmentSources()
    {
        environment_source_registry_.Drain();
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

    void RenderSystem::DestroyMaterialAssetRecords()
    {
        if (material_asset_resolver_)
        {
            material_asset_resolver_->Clear();
            material_asset_resolver_.reset();
        }
    }

    void RenderSystem::ApplyDefaultCamera()
    {
        scene_camera_.SetPosition({0.f, 0.f, 300.f});
        scene_camera_.SetRotation({0.f, -90.f, 0.f});
        scene_camera_.SetProjectionMode(CameraProjectionMode::Perspective);
        scene_camera_.SetFOV(45.f);
        scene_camera_.SetNearPlane(1.f);
        scene_camera_.SetFarPlane(2000.f);
        scene_camera_.SetOrthographicHeight(10.f);
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

    bool RenderSystem::IsState(RenderSystemLifecycleState expected) const
    {
        return lifecycle_state_ == expected;
    }

    void RenderSystem::CleanupOwnedState()
    {
        // If teardown is requested between BeginFrame and EndFrame, close the
        // frame bracket before waiting or destroying any frame-owned resource.
        if (lifecycle_state_ == RenderSystemLifecycleState::FrameActive && backend_)
        {
            if (deferred_renderer_)
            {
                deferred_renderer_->FinalizeFrame();
            }
            if (active_frame_context_)
            {
                active_frame_context_->End();
                active_frame_context_ = nullptr;
            }
            backend_->EndFrame();
        }

        camera_source_registry_.Clear();
        environment_source_registry_.Clear();
        source_registry_.Clear(render_world_);
        render_world_.ApplyPendingCommands();
        render_world_.Clear();
        light_source_registry_.Clear(light_world_);
        light_world_.Clear();
        render_cache_.clear();
        DestroyMaterialAssetRecords();
        material_asset_resolver_.reset();
        material_system_.reset();

        if (backend_ && backend_initialized_)
        {
            // Releasing material instances first retires their bindless table
            // slots. WaitIdle then makes all submitted GPU work safe to retire.
            backend_->WaitIdle();
            if (graphics::IRenderTargetReadback *const readback =
                    backend_->GetRenderTargetReadback())
            {
                readback->DrainPendingReadbacks("Render system shutdown");
            }
        }
        render_capture_service_.reset();

        for (FrameContext &context : frame_contexts_)
        {
            context.Cleanup();
        }
        frame_contexts_.clear();
        if (deferred_renderer_)
        {
            deferred_renderer_->Cleanup();
            deferred_renderer_.reset();
        }

        if (resource_resolver_)
        {
            resource_resolver_->Cleanup();
            resource_resolver_.reset();
        }
        resource_pipeline_.reset();
        if (backend_)
        {
            backend_->Cleanup();
            backend_.reset();
        }
        backend_initialized_ = false;
        load_queue_ = nullptr;
        active_frame_context_ = nullptr;
        frame_number_ = 0;
        elapsed_seconds_ = 0.0f;
    }

    void RenderSystem::Shutdown()
    {
        if (lifecycle_state_ == RenderSystemLifecycleState::ShutDown)
        {
            return;
        }
        CleanupOwnedState();
        lifecycle_state_ = RenderSystemLifecycleState::ShutDown;
    }
}
