#include "render_system.h"

#include <algorithm>
#include <array>
#include <limits>
#include <utility>

#include "asset/asset_manager.h"
#include "asset/mesh.h"
#include "asset/model.h"
#include "asset/shader.h"
#include "asset/shader_program.h"
#include "asset/texture.h"
#include "config/path.h"
#include "graphics/backend/common/render_backend.h"
#include "graphics/backend/common/command_recorder.h"
#include "log/logger.h"
#include "render/material/material_system.h"
#include "render/material/material_asset_resolver.h"
#include "render/render_capture_service_internal.h"
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

        spatial::AABB TransformBounds(const spatial::AABB &local_bounds,
                                      const Transform3f &transform)
        {
            if (!local_bounds.IsValid())
            {
                return local_bounds;
            }

            const std::array<Vector3f, 8> corners{{
                {local_bounds.min_.x_, local_bounds.min_.y_, local_bounds.min_.z_},
                {local_bounds.min_.x_, local_bounds.min_.y_, local_bounds.max_.z_},
                {local_bounds.min_.x_, local_bounds.max_.y_, local_bounds.min_.z_},
                {local_bounds.min_.x_, local_bounds.max_.y_, local_bounds.max_.z_},
                {local_bounds.max_.x_, local_bounds.min_.y_, local_bounds.min_.z_},
                {local_bounds.max_.x_, local_bounds.min_.y_, local_bounds.max_.z_},
                {local_bounds.max_.x_, local_bounds.max_.y_, local_bounds.min_.z_},
                {local_bounds.max_.x_, local_bounds.max_.y_, local_bounds.max_.z_},
            }};

            const float maximum = std::numeric_limits<float>::max();
            spatial::AABB world_bounds{{maximum, maximum, maximum},
                                       {-maximum, -maximum, -maximum}};
            for (const Vector3f &corner : corners)
            {
                const Vector3f transformed =
                    transform.rotator_.RotateVector(transform.scale_ * corner) +
                    transform.position_;
                world_bounds.min_.x_ = std::min(world_bounds.min_.x_, transformed.x_);
                world_bounds.min_.y_ = std::min(world_bounds.min_.y_, transformed.y_);
                world_bounds.min_.z_ = std::min(world_bounds.min_.z_, transformed.z_);
                world_bounds.max_.x_ = std::max(world_bounds.max_.x_, transformed.x_);
                world_bounds.max_.y_ = std::max(world_bounds.max_.y_, transformed.y_);
                world_bounds.max_.z_ = std::max(world_bounds.max_.z_, transformed.z_);
            }
            return world_bounds;
        }
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
        material_asset_resolver_ = std::make_unique<MaterialAssetResolver>(*material_system_);

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
        frame_targets_.Initialize(*backend_, extent.width, extent.height);
        if (!frame_targets_.GetTarget(RenderTargetName::SceneColor)->IsValid())
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR, "Failed to create scene render target");
        }
        render_capture_service_ = std::make_unique<RenderCaptureService>(
            backend_->GetRenderTargetReadback(),
            [this] { return frame_targets_.GetTarget(RenderTargetName::SceneColor)->GetHandle(); },
            [this] { return frame_number_; });
        ConfigurePassSchedule();
        // Frame the ~165-unit rock bootstrap fixture: pull the camera back and
        // extend the far plane (the old default far=10 culled it entirely).
        scene_camera_.SetPosition({0.f, 0.f, 300.f});
        scene_camera_.SetNearPlane(1.f);
        scene_camera_.SetFarPlane(2000.f);
    }

    void RenderSystem::PostInitialize()
    {
        // Bootstrap: load + process everything already queued, before the main loop.
        ConsumeRequests(0);
        PrepareBootstrapRenderableSources();
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
        DrainRenderableSources();
        render_world_.ApplyPendingCommands();
        DrainLightSources();
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
            active_frame_context_->Begin(
                backend_->GetCurrentFrameIndex(),
                {frame_number_, elapsed_seconds_, delta_time},
                {frame_targets_.GetTarget(RenderTargetName::SceneColor)->GetWidth(),
                 frame_targets_.GetTarget(RenderTargetName::SceneColor)->GetHeight()});
            frame_lighting_binding_ = active_frame_context_->CreateLightingBinding(
                BuildLightGpuFrameData(light_world_.Snapshot()));
            editor_composite_recorded_ = false;
            RecordGBufferPass();
            RecordGBufferDebugViewPass();
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
            frame_lighting_binding_ = {};
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

    IRenderCaptureService *RenderSystem::GetRenderCaptureService()
    {
        return render_capture_service_.get();
    }

    void RenderSystem::ConfigurePassSchedule()
    {
        // D3 deferred schedule: an opaque-only GBuffer write, a debug composite
        // that reads the G-buffer and writes SceneColor, then the editor's
        // terminal SceneColor read. The unlit ScenePass is retired from the
        // engine schedule; the smoke keeps exercising it.
        const bool added_gbuffer = pass_schedule_.AddPass(
            {"GBufferPass", {{RenderPassResource::GBuffer, RenderPassAccess::Write}}, false});
        const bool added_debug_view = pass_schedule_.AddPass(
            {"GBufferDebugViewPass",
             {{RenderPassResource::GBuffer, RenderPassAccess::Read},
              {RenderPassResource::SceneColor, RenderPassAccess::Write}},
             false});
        const bool added_editor = pass_schedule_.AddPass(
            {"EditorCompositePass", {{RenderPassResource::SceneColor, RenderPassAccess::Read}}, true});
        std::string error;
        if (!added_gbuffer || !added_debug_view || !added_editor ||
            !pass_schedule_.Validate(error))
        {
            KP_LOG("RenderLog", LOG_LEVEL_ERROR, "Invalid render pass schedule: %s", error.c_str());
        }
    }

    void RenderSystem::RecordGBufferPass()
    {
        if (!active_frame_context_ || !pass_schedule_.IsValid())
        {
            return;
        }

        graphics::CommandRecorder *const recorder = backend_->GetCommandRecorder();
        RenderTarget *const gbuffer_target = frame_targets_.GetTarget(RenderTargetName::GBuffer);
        if (!recorder || !gbuffer_target || !gbuffer_target->BeginRecording(*recorder))
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
            // Opaque-only for the deferred G-buffer; alpha-blended surfaces need
            // a forward pass (a later roadmap step), so they are skipped here.
            const SceneDrawLists draw_lists = SceneDrawListBuilder::Build(
                visible_proxies, *material_system_, *resource_resolver_, MaterialPass::GBuffer);
            for (const SceneDrawItem &item : draw_lists.opaque)
            {
                RecordMeshProxy(item.proxy, per_pass_data, *recorder, MaterialPass::GBuffer);
            }
        }
        gbuffer_target->EndRecording(*recorder);
    }

    void RenderSystem::RecordGBufferDebugViewPass()
    {
        if (!active_frame_context_ || !pass_schedule_.IsValid())
        {
            return;
        }

        graphics::CommandRecorder *const recorder = backend_->GetCommandRecorder();
        RenderTarget *const scene_target = frame_targets_.GetTarget(RenderTargetName::SceneColor);
        RenderTarget *const gbuffer_target = frame_targets_.GetTarget(RenderTargetName::GBuffer);
        if (!recorder || !scene_target || !gbuffer_target ||
            !PrepareGBufferDebugPassResources())
        {
            return;
        }
        const graphics::Extent2D extent = active_frame_context_->GetRenderExtent();
        if (extent.width == 0 || extent.height == 0 ||
            !scene_target->BeginRecording(*recorder))
        {
            return;
        }
        (void)extent;

        // Composite the three G-buffer color attachments into SceneColor so the
        // existing capture/editor path stays green. D5's lighting replaces this
        // debug view; the attachments are sampled linear (non-sRGB formats).
        const graphics::DescriptorSetHandle debug_bindings =
            active_frame_context_->AllocateResourceBindingSet(
                gbuffer_debug_pipeline_,
                {0,
                 {graphics::SampledTextureBinding{
                      0, 2, gbuffer_target->GetColorAttachmentTexture(0),
                      gbuffer_debug_sampler_},
                  graphics::SampledTextureBinding{
                      0, 3, gbuffer_target->GetColorAttachmentTexture(1),
                      gbuffer_debug_sampler_},
                  graphics::SampledTextureBinding{
                      0, 4, gbuffer_target->GetColorAttachmentTexture(2),
                      gbuffer_debug_sampler_}}});
        if (debug_bindings.IsValid())
        {
            recorder->BindPipeline(gbuffer_debug_pipeline_);
            recorder->BindMesh(gbuffer_debug_fullscreen_mesh_);
            recorder->BindResourceBindings(gbuffer_debug_pipeline_, debug_bindings);
            recorder->DrawIndexed();
        }
        scene_target->EndRecording(*recorder);
    }

    bool RenderSystem::PrepareGBufferDebugPassResources()
    {
        if (gbuffer_debug_pipeline_.IsValid() && gbuffer_debug_fullscreen_mesh_.IsValid() &&
            gbuffer_debug_sampler_.IsValid())
        {
            return true;
        }

        auto &asset_manager = asset::AssetManager::GetInstance();
        const asset::AssetID program_id = asset_manager.LoadSync(
            GetShaderDirectory() + "gbuffer_debug_view.shader");
        auto program = asset_manager.GetResource<asset::ShaderProgramResource>(program_id);
        if (!program)
        {
            return false;
        }
        const auto shaders = program->GatherShaders(asset::ShaderProgramVariant::Bound);
        resource_pipeline_->ProcessShader(shaders);
        for (const asset::ShaderPtr &shader : shaders)
        {
            if (!shader || shader->status == asset::ShaderStatus::CompileFailed)
            {
                return false;
            }
        }
        const auto vert_shader = program->GetShader(
            ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL);
        const auto frag_shader = program->GetShader(
            ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL);
        if (!vert_shader || !frag_shader || !vert_shader->data || !frag_shader->data)
        {
            return false;
        }

        graphics::PipelineDesc desc{};
        desc.vert_shader = vert_shader->data.get();
        desc.frag_shader = frag_shader->data.get();
        desc.color_attachment_formats = {TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB};
        desc.depth_attachment_format = TextureFormat::TEXTURE_FORMAT_UNKNOW;
        desc.binding_descs = {{0, sizeof(data::Vertex), false}};
        desc.attri_descs = {
            {0, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS,
             offsetof(data::Vertex, position)},
            {1, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS,
             offsetof(data::Vertex, tex_coord)},
        };
        // Fullscreen triangle: front-facing in OpenGL y-up NDC, back-facing in
        // Vulkan y-down NDC, so the pass must not cull.
        desc.raster_state.front_face = graphics::FrontFace::FRONT_FACE_COUNTER_CLOCKWISE;
        desc.raster_state.cull_mode = graphics::CullMode::CULL_MODE_NONE;
        desc.descriptor_binding_descs = {
            {{2, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
              ShaderStage::SHADER_STAGE_FRAGMENT},
             {3, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
              ShaderStage::SHADER_STAGE_FRAGMENT},
             {4, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
              ShaderStage::SHADER_STAGE_FRAGMENT}},
        };
        gbuffer_debug_pipeline_ = backend_->CreatePipelineResource(desc);

        data::MeshData fullscreen_mesh{};
        data::Vertex v0{}, v1{}, v2{};
        v0.position = {-1.0f, -1.0f, 0.0f};
        v0.tex_coord = {0.0f, 0.0f};
        v1.position = {3.0f, -1.0f, 0.0f};
        v1.tex_coord = {2.0f, 0.0f};
        v2.position = {-1.0f, 3.0f, 0.0f};
        v2.tex_coord = {0.0f, 2.0f};
        fullscreen_mesh.vertices = {v0, v1, v2};
        fullscreen_mesh.indices = {0, 1, 2};
        fullscreen_mesh.sections = {{0, 3, 0}};
        gbuffer_debug_fullscreen_mesh_ = backend_->CreateMesh(fullscreen_mesh);
        gbuffer_debug_sampler_ = backend_->CreateSampler(graphics::SamplerSettings{});

        if (!gbuffer_debug_pipeline_.IsValid() || !gbuffer_debug_fullscreen_mesh_.IsValid() ||
            !gbuffer_debug_sampler_.IsValid())
        {
            return false;
        }
        return true;
    }

    void RenderSystem::RecordMeshProxy(const MeshProxy &proxy,
                                       const graphics::PerPassData &per_pass_data,
                                       graphics::CommandRecorder &recorder,
                                       MaterialPass pass)
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
            *material_system_, *resource_resolver_, proxy.material, draw_bindings, pass);
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
        source_registry_.Drain(
            render_world_, [this](const PrimitiveRenderableSourceDesc &source)
            { return ResolveRenderableSource(source); });
    }

    void RenderSystem::DrainLightSources()
    {
        light_source_registry_.Drain(light_world_);
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

    std::optional<StaticMeshRenderableSourceDesc> RenderSystem::TakeBootstrapRenderableSource()
    {
        if (bootstrap_renderable_sources_.empty())
        {
            return std::nullopt;
        }
        StaticMeshRenderableSourceDesc source = std::move(bootstrap_renderable_sources_.front());
        bootstrap_renderable_sources_.erase(bootstrap_renderable_sources_.begin());
        return source;
    }

    std::vector<StaticMeshRenderableSourceDesc> RenderSystem::TakeBootstrapRenderableSources()
    {
        return std::exchange(bootstrap_renderable_sources_, {});
    }

    void RenderSystem::PrepareBootstrapRenderableSources()
    {
        if ((!bootstrap_scene_info_.IsComplete() && bootstrap_scene_info_.objects.empty()) ||
            !bootstrap_renderable_sources_.empty())
        {
            return;
        }

        auto &asset_manager = asset::AssetManager::GetInstance();
        std::vector<BootstrapSceneObjectInfo> objects;
        if (bootstrap_scene_info_.IsComplete())
        {
            objects.push_back({bootstrap_scene_info_.model_path,
                               bootstrap_scene_info_.material_path,
                               Transform3f{}});
        }
        objects.insert(objects.end(), bootstrap_scene_info_.objects.begin(),
                       bootstrap_scene_info_.objects.end());

        for (const BootstrapSceneObjectInfo &object : objects)
        {
            const asset::AssetID model_asset = asset_manager.LoadSync(object.model_path);
            const asset::AssetID material_asset = asset_manager.LoadSync(object.material_path);
            const auto model = asset_manager.GetResource<asset::ModelResource>(model_asset);
            const asset::AssetID mesh_asset = model
                                                  ? model->GetData(asset::ModelGeometryType::KPMG_Mesh)
                                                  : asset::AssetID{};
            if (!mesh_asset.IsValid() || !material_asset.IsValid())
            {
                KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                       "Bootstrap scene object could not prepare a mesh or material asset");
                continue;
            }

            StaticMeshRenderableSourceDesc source{};
            source.mesh_asset = mesh_asset;
            source.material_asset = material_asset;
            source.world_transform = object.world_transform;
            const auto mesh = asset_manager.GetResource<asset::MeshResource>(mesh_asset);
            if (!mesh || !mesh->data || mesh->data->vertices.empty())
            {
                KP_LOG("RenderLog", LOG_LEVEL_ERROR,
                       "Bootstrap scene object mesh has no geometry bounds");
                continue;
            }
            source.local_bounds = mesh->local_bounds;
            source.world_bounds = TransformBounds(source.local_bounds, source.world_transform);
            bootstrap_renderable_sources_.push_back(std::move(source));
        }
        KP_LOG("RenderLog", LOG_LEVEL_INFO, "Bootstrap gameplay render source(s) prepared: %zu",
               bootstrap_renderable_sources_.size());
    }

    void RenderSystem::DestroyMaterialAssetRecords()
    {
        if (material_asset_resolver_)
        {
            material_asset_resolver_->Clear();
            material_asset_resolver_.reset();
        }
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
        // RebuildForExtent retires via WaitIdle internally only when the extent
        // changed, so a stable size keeps the shared target's GPU generations
        // intact across frames. active_frame_context_ is nulled here to match the
        // old pre-rebuild boundary; the next BeginFrame re-acquires it.
        frame_targets_.RebuildForExtent(*backend_, requested.width, requested.height);
        active_frame_context_ = nullptr;
        if (!frame_targets_.GetTarget(RenderTargetName::SceneColor)->IsValid())
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
            frame_lighting_binding_ = {};
            render_capture_service_.reset();
            source_registry_.Clear(render_world_);
            render_world_.Clear();
            light_source_registry_.Clear(light_world_);
            light_world_.Clear();
            bootstrap_renderable_sources_.clear();
            DestroyMaterialAssetRecords();
            material_system_.reset();
            resource_resolver_.reset();
            return;
        }

        // Releasing material instances first retires their bindless table slots.
        // WaitIdle must follow that release so Graphics can collect the retired
        // table references before the resolver destroys cached textures/samplers.
        source_registry_.Clear(render_world_);
        render_world_.ApplyPendingCommands();
        render_world_.Clear();
        light_source_registry_.Clear(light_world_);
        light_world_.Clear();
        frame_lighting_binding_ = {};
        bootstrap_renderable_sources_.clear();
        DestroyMaterialAssetRecords();
        material_system_.reset();
        backend_->WaitIdle();
        if (graphics::IRenderTargetReadback *const readback = backend_->GetRenderTargetReadback())
        {
            readback->DrainPendingReadbacks("Render system shutdown");
        }
        render_capture_service_.reset();
        for (FrameContext &context : frame_contexts_)
        {
            context.Cleanup();
        }
        frame_contexts_.clear();
        frame_targets_.Cleanup();
        if (gbuffer_debug_pipeline_.IsValid())
        {
            backend_->DestroyPipelineResource(gbuffer_debug_pipeline_);
        }
        if (gbuffer_debug_fullscreen_mesh_.IsValid())
        {
            backend_->DestroyMesh(gbuffer_debug_fullscreen_mesh_);
        }
        if (gbuffer_debug_sampler_.IsValid())
        {
            backend_->DestroySampler(gbuffer_debug_sampler_);
        }
        gbuffer_debug_pipeline_ = {};
        gbuffer_debug_fullscreen_mesh_ = {};
        gbuffer_debug_sampler_ = {};
        resource_resolver_->Cleanup();
        resource_resolver_.reset();
        backend_->Cleanup();
        backend_.reset();
    }
}
