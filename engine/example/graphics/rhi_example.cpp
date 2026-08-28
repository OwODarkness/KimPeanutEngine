
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "runtime/window/glfw_window_system.h"
#include "runtime/input/input_system.h"
#include "runtime/input/input_context.h"
#include "runtime/render/render_camera.h"
#include "runtime/render/frame_context.h"
#include "runtime/render/render_resource_resolver.h"
#include "runtime/render/material/material_system.h"
#include "runtime/render/render_world/render_world.h"
#include "runtime/render/render_source_registry.h"
#include "runtime/gameplay/actor/actor.h"
#include "runtime/gameplay/component/mesh_component.h"
#include "runtime/gameplay/world/gameplay_world.h"
#include "runtime/graphics/backend/common/pipeline_types.h"
#include "runtime/graphics/backend/common/render_backend.h"
#include "runtime/graphics/backend/common/render_target.h"
#include "runtime/graphics/backend/common/resource_binding.h"
#include "runtime/graphics/backend/common/sampler.h"
#include "runtime/graphics/backend/common/texture.h"
#include "runtime/core/data/shader.h"
#include "runtime/core/data/mesh.h"
#include "runtime/core/config/path.h"
#include "runtime/asset/asset_manager.h"
#include "runtime/asset/model.h"
#include "runtime/asset/mesh.h"
#include "runtime/asset/shader.h"
#include "runtime/asset/shader_program.h"
#include "runtime/asset/texture.h"
#include "runtime/core/resource/resource_pipeline.h"

namespace kpengine::example
{
    // The demo bakes its shaders at runtime through the resource pipeline
    // (GLSL -> per-API artifact, content-addressed cache) instead of reading
    // prebuilt files — the RHI never reads shader files itself.
    static asset::AssetID LoadShaderProgram(resource::ResourcePipeline &pipeline,
                                            const std::string &program_file)
    {
        const std::string path = GetShaderDirectory() + program_file;
        const asset::AssetID id = asset::AssetManager::GetInstance().LoadSync(path);
        if (!id.IsValid())
        {
            throw std::runtime_error("failed to load shader program: " + path);
        }
        auto program = asset::AssetManager::GetInstance().GetResource<asset::ShaderProgramResource>(id);
        if (!program)
        {
            throw std::runtime_error("loaded asset holds no shader program resource");
        }

        // This direct smoke pipeline uses the conventional bound ABI. The
        // Material checks below resolve their own bound and bindless variants.
        pipeline.ProcessShader(program->GatherShaders(asset::ShaderProgramVariant::Bound));
        return id;
    }

    struct DemoResourceHandles
    {
        graphics::MeshHandle mesh;
    };

    static DemoResourceHandles CreateDemoResources(graphics::RenderBackend &backend)
    {
        DemoResourceHandles handles{};

        const asset::AssetID model_id = asset::AssetManager::GetInstance().LoadSync(
            GetModelDirectory() + "sphere/sphere.obj");
        auto model = asset::AssetManager::GetInstance().GetResource<asset::ModelResource>(model_id);
        auto mesh = model ? model->GetMesh() : nullptr;
        if (!mesh || !mesh->data)
        {
            throw std::runtime_error("failed to load demo mesh");
        }
        handles.mesh = backend.CreateMesh(*mesh->data);
        const asset::AssetID mesh_id = model->GetData(asset::ModelGeometryType::KPMG_Mesh);
        asset::AssetManager::GetInstance().UnRegisterAsset(model_id);
        asset::AssetManager::GetInstance().UnRegisterAsset(mesh_id);

        return handles;
    }

    static bool RunRHI(GraphicsAPIType api, uint32_t max_frames, bool trigger_resize)
    {
        try
        {
            std::unique_ptr<WindowSystem> window = WindowSystem::CreateWindowSystem(WindowAPIType::WINDOW_API_GLFW);
            WindowCreateInfo window_create_info;
            window_create_info.graphics_api_type = api;
            window_create_info.width = 1600;
            window_create_info.height = 1024;
            window_create_info.title = "RHI";
            window->Initialize(window_create_info);

            std::unique_ptr<input::InputSystem> input = std::make_unique<input::InputSystem>();
            input->BindKeyEvent(window->key_event_dispatcher_);
            input->BindCursorEvent(window->cursor_event_dispatcher_);
            input->BindScrollEvent(window->scroll_event_dispatcher_);
            std::shared_ptr<input::InputContext> context = std::make_shared<input::InputContext>();
            input->AddContext("SceneInputContext", context);
            input->SetActiveContext("SceneInputContext");

            resource::ResourcePipeline resource_pipeline;
            resource_pipeline.Initialize({api});
            const asset::AssetID shader_program_id = LoadShaderProgram(
                resource_pipeline, "simple_triangle.shader");
            auto program = asset::AssetManager::GetInstance().GetResource<asset::ShaderProgramResource>(
                shader_program_id);
            const auto vertex_shader = program ? program->GetShader(
                ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL) : nullptr;
            const auto fragment_shader = program ? program->GetShader(
                ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL) : nullptr;
            if (!vertex_shader || !fragment_shader || !vertex_shader->data || !fragment_shader->data)
            {
                throw std::runtime_error("triangle shaders failed to compile");
            }

            graphics::PipelineDesc pipeline_desc{};
            pipeline_desc.vert_shader = vertex_shader->data.get();
            pipeline_desc.frag_shader = fragment_shader->data.get();
            pipeline_desc.color_attachment_formats = {TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB};
            pipeline_desc.depth_attachment_format = TextureFormat::TEXTURE_FORMAT_D32;

            pipeline_desc.binding_descs = {{0, sizeof(data::Vertex), false}};
            pipeline_desc.attri_descs = {
                {0, 0, graphics::VertexFormat::VERTEX_FORMAT_THREE_FLOATS, offsetof(data::Vertex, position)},
                {1, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS, offsetof(data::Vertex, tex_coord)},
            };

            pipeline_desc.descriptor_binding_descs = {
                {{0, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM, ShaderStage::SHADER_STAGE_VERTEX},
                 {1, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM, ShaderStage::SHADER_STAGE_VERTEX},
                 {2, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER, ShaderStage::SHADER_STAGE_FRAGMENT},
                 {3, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM, ShaderStage::SHADER_STAGE_FRAGMENT}},
            };

            graphics::RasterState raster_state{};
            raster_state.cull_mode = graphics::CullMode::CULL_MODE_BACK;
            raster_state.front_face = graphics::FrontFace::FRONT_FACE_COUNTER_CLOCKWISE;
            pipeline_desc.raster_state = raster_state;

            std::unique_ptr<graphics::RenderBackend> rhi = graphics::RenderBackend::CreateGraphicsBackEnd(api);
            rhi->BindWindowResize(window->resize_event_dispatcher_);
            rhi->Initialize(window->GetNativeHandle());
            const bool bindless_enabled = rhi->GetCapabilities().SupportsBindlessTextures();
            std::cout << (api == GraphicsAPIType::GRAPHICS_API_VULKAN ? "Vulkan" : "OpenGL")
                      << " bindless textures: "
                      << (bindless_enabled ? "enabled" : "unavailable (bound fallback)")
                      << std::endl;
            // Force the Vulkan dedicated-allocation policy while retaining the
            // same public RHI path on OpenGL. The smoke only touches the first
            // word; the resource exists to verify large mapped-buffer teardown.
            constexpr uint32_t kDedicatedSmokeBufferSize = 4 * 1024 * 1024 + 1;
            const graphics::BufferHandle dedicated_smoke_buffer =
                rhi->CreateUniformBuffer(kDedicatedSmokeBufferSize);
            uint32_t *const dedicated_smoke_data = static_cast<uint32_t *>(
                rhi->MapUniformBuffer(dedicated_smoke_buffer, sizeof(uint32_t)));
            if (!dedicated_smoke_buffer.IsValid() || !dedicated_smoke_data)
            {
                throw std::runtime_error("failed to create mapped dedicated smoke buffer");
            }
            *dedicated_smoke_data = 0xC0DEC0DEu;
            graphics::RenderTargetDesc render_target_desc{};
            render_target_desc.width = static_cast<uint32_t>(window_create_info.width);
            render_target_desc.height = static_cast<uint32_t>(window_create_info.height);
            const graphics::RenderTargetHandle scene_target =
                rhi->CreateRenderTarget(render_target_desc);
            if (!scene_target.IsValid() || !rhi->GetRenderTargetColor(scene_target).IsValid())
            {
                throw std::runtime_error("failed to create scene render target");
            }
            const graphics::PipelineHandle secondary_pipeline = rhi->CreatePipelineResource(pipeline_desc);
            if (!secondary_pipeline.IsValid())
            {
                throw std::runtime_error("failed to create secondary graphics pipeline");
            }

            render::RenderResourceResolver resource_resolver(*rhi, resource_pipeline);
            render::MaterialSystem materials;
            materials.SetResourceResolver(&resource_resolver);

            std::vector<render::FrameContext> frame_contexts(rhi->GetFramesInFlight());
            for (render::FrameContext &frame_context : frame_contexts)
            {
                frame_context.Initialize(*rhi, 64 * 1024);
            }

            DemoResourceHandles demo_resources = CreateDemoResources(*rhi);
            const asset::AssetID texture_id = asset::AssetManager::GetInstance().LoadSync(
                GetTextureDirectory() + "wallpaper.jpg");
            const asset::AssetID alternate_texture_id = asset::AssetManager::GetInstance().LoadSync(
                GetTextureDirectory() + "default.png");
            if (!texture_id.IsValid() || !alternate_texture_id.IsValid())
            {
                throw std::runtime_error("failed to load multi-material smoke textures");
            }
            render::MaterialTemplateDesc material_template_desc{};
            material_template_desc.shader_program = shader_program_id;
            material_template_desc.bindless_texture_table_compatible = true;
            material_template_desc.shading_model = render::MaterialShadingModel::Unlit;
            material_template_desc.parameters = {
                {"base_color", Vector4f{1.0f, 1.0f, 1.0f, 1.0f}},
                {"base_color_texture", render::MaterialTextureSamplerValue{texture_id, {}}, 2},
            };
            const render::MaterialTemplateHandle material_template =
                materials.CreateTemplate(material_template_desc);
            auto bound_material_template_desc = material_template_desc;
            bound_material_template_desc.bindless_texture_table_compatible = false;
            const render::MaterialTemplateHandle bound_material_template =
                materials.CreateTemplate(bound_material_template_desc);
            const render::MaterialInstanceHandle first_material_instance =
                materials.CreateInstance({material_template, {}});
            const render::MaterialInstanceHandle second_material_instance = materials.CreateInstance(
                {material_template,
                 {{render::MaterialParameterID{1},
                   render::MaterialTextureSamplerValue{alternate_texture_id, {}}}}});
            const render::MaterialInstanceHandle first_bound_material_instance =
                materials.CreateInstance({bound_material_template, {}});
            const render::MaterialInstanceHandle second_bound_material_instance = materials.CreateInstance(
                {bound_material_template,
                 {{render::MaterialParameterID{1},
                   render::MaterialTextureSamplerValue{alternate_texture_id, {}}}}});
            if (!first_material_instance.IsValid() || !second_material_instance.IsValid() ||
                !first_bound_material_instance.IsValid() || !second_bound_material_instance.IsValid())
            {
                throw std::runtime_error("failed to create multi-material smoke instances");
            }
            for (const render::MaterialInstanceHandle instance :
                 {first_material_instance, second_material_instance})
            {
                const bool uses_bindless = resource_resolver.UsesBindlessTextures(instance);
                if (uses_bindless != bindless_enabled)
                {
                    throw std::runtime_error("material binding mode did not match backend capability");
                }
                const auto *resolved = resource_resolver.FindTextureBindings(instance);
                if (!resolved || (bindless_enabled &&
                                  resolved->bindless_slots.size() != resolved->textures.size()))
                {
                    throw std::runtime_error("failed to resolve all multi-material bindless slots");
                }
            }
            for (const render::MaterialInstanceHandle instance :
                 {first_bound_material_instance, second_bound_material_instance})
            {
                if (resource_resolver.UsesBindlessTextures(instance))
                {
                    throw std::runtime_error("ordinary material unexpectedly selected bindless textures");
                }
            }
            render::RenderWorld render_world{};
            render::MeshProxyDesc proxy_desc{};
            proxy_desc.mesh = demo_resources.mesh;
            proxy_desc.material = first_material_instance;
            proxy_desc.world_transform.position_ = {-0.75f, 0.0f, 0.0f};
            proxy_desc.world_transform.scale_ = {0.25f, 0.25f, 0.25f};
            const render::RenderableHandle first_renderable = render_world.EnqueueCreate(proxy_desc);
            proxy_desc.material = second_material_instance;
            proxy_desc.world_transform.position_ = {-0.25f, 0.0f, 0.0f};
            const render::RenderableHandle second_renderable = render_world.EnqueueCreate(proxy_desc);
            proxy_desc.material = first_bound_material_instance;
            proxy_desc.world_transform.position_ = {0.25f, 0.0f, 0.0f};
            const render::RenderableHandle first_bound_renderable = render_world.EnqueueCreate(proxy_desc);
            proxy_desc.material = second_bound_material_instance;
            proxy_desc.world_transform.position_ = {0.75f, 0.0f, 0.0f};
            const render::RenderableHandle second_bound_renderable = render_world.EnqueueCreate(proxy_desc);
            render::RenderableSourceRegistry source_registry{};
            gameplay::GameplayWorld gameplay_world{&source_registry};
            const gameplay::ActorHandle gameplay_actor_handle = gameplay_world.CreateActor();
            gameplay::Actor *const gameplay_actor = gameplay_world.FindActor(gameplay_actor_handle);
            if (!gameplay_actor)
            {
                throw std::runtime_error("failed to create gameplay smoke actor");
            }
            auto *const gameplay_mesh = gameplay_actor->AddComponent<gameplay::MeshComponent>();
            if (!gameplay_mesh || !gameplay_actor->SetRootComponent(gameplay_mesh))
            {
                throw std::runtime_error("failed to create gameplay smoke mesh component");
            }
            gameplay_mesh->SetMeshAsset({1, 0, asset::AssetType::KPAT_Mesh});
            gameplay_mesh->SetMaterialAsset({1, 0, asset::AssetType::KPAT_Material});
            gameplay_mesh->SetLocalBounds({{-1.0f, -1.0f, -1.0f}, {1.0f, 1.0f, 1.0f}});
            gameplay_mesh->SetLocalLocation({0.0f, 0.5f, 0.0f});
            gameplay_mesh->SetLocalScale({0.2f, 0.2f, 0.2f});
            if (!gameplay_world.InitializeActor(gameplay_actor_handle) ||
                !gameplay_world.ActivateActor(gameplay_actor_handle))
            {
                throw std::runtime_error("failed to activate gameplay smoke actor");
            }
            const auto resolve_gameplay_source =
                [&demo_resources, first_material_instance](const render::PrimitiveRenderableSourceDesc &source)
            {
                const auto &static_mesh = std::get<render::StaticMeshRenderableSourceDesc>(source);
                render::MeshProxyDesc resolved{};
                resolved.mesh = demo_resources.mesh;
                resolved.material = first_material_instance;
                resolved.world_transform = static_mesh.world_transform;
                resolved.world_bounds = static_mesh.world_bounds;
                resolved.flags = static_mesh.flags;
                resolved.lod_bias = static_mesh.lod_bias;
                return render::RenderableSourceResolution{
                    render::RenderableSourceState::Ready, {}, resolved};
            };
            gameplay_world.Tick(0.0f);
            source_registry.Drain(render_world, resolve_gameplay_source);
            render_world.ApplyPendingCommands();
            if (!render_world.IsRegistered(first_renderable) ||
                !render_world.IsRegistered(second_renderable) ||
                !render_world.IsRegistered(first_bound_renderable) ||
                !render_world.IsRegistered(second_bound_renderable))
            {
                throw std::runtime_error("failed to register multi-material smoke mesh proxies");
            }
            if (render_world.Snapshot().size() != 5U)
            {
                throw std::runtime_error("gameplay smoke mesh did not reach RenderWorld");
            }
            render::RenderCamera camera{};

            uint64_t frame_number = 0;
            float elapsed_seconds = 0.0f;
            constexpr float kDemoDeltaSeconds = 1.0f / 60.0f;
            uint32_t rendered_frames = 0;
            while (!window->ShouldClose() && (max_frames == 0 || rendered_frames < max_frames))
            {
                window->PollEvents();
                if (rendered_frames == 0)
                {
                    gameplay_mesh->SetLocalLocation({0.0f, -0.5f, 0.0f});
                }
                else if (rendered_frames == 1)
                {
                    gameplay_mesh->SetVisible(false);
                }
                else if (rendered_frames == 2)
                {
                    gameplay_mesh->SetVisible(true);
                }
                gameplay_world.Tick(kDemoDeltaSeconds);
                source_registry.Drain(render_world, resolve_gameplay_source);
                render_world.ApplyPendingCommands();
                if (trigger_resize && rendered_frames == 1)
                {
                    window->SetWindowSize(1024, 768);
                    window->resize_event_dispatcher_.Dispatch({1024, 768});
                }
                rhi->BeginFrame();
                if (rhi->GetCommandRecorder())
                {
                    const uint32_t frame_index = rhi->GetCurrentFrameIndex();
                    if (frame_index < frame_contexts.size())
                    {
                        render::FrameContext &frame_context = frame_contexts[frame_index];
                        elapsed_seconds += kDemoDeltaSeconds;
                        frame_context.Begin(frame_index,
                                           {frame_number, elapsed_seconds, kDemoDeltaSeconds},
                                           rhi->GetRenderExtent());
                        graphics::CommandRecorder *recorder = rhi->GetCommandRecorder();
                        recorder->BeginRenderTarget(scene_target);
                        const graphics::Extent2D extent = frame_context.GetRenderExtent();
                        if (extent.height != 0)
                        {
                            camera.SetAspect(static_cast<float>(extent.width) /
                                             static_cast<float>(extent.height));
                            const render::CameraData camera_data = camera.GetCameraData();
                            graphics::PerPassData per_pass_data{};
                            per_pass_data.camera_data.view = camera_data.view;
                            per_pass_data.camera_data.proj = camera_data.proj;
                            const render::UniformAllocation per_pass =
                                frame_context.AllocateUniform(per_pass_data);
                            for (const render::MeshProxy &proxy : render_world.Snapshot())
                            {
                                if (!proxy.flags.visible || !proxy.mesh.IsValid() ||
                                    !per_pass.IsValid())
                                {
                                    continue;
                                }
                                graphics::PerObjectData per_object_data{};
                                per_object_data.model = Matrix4f::MakeTransformMatrix(
                                    proxy.world_transform).Transpose();
                                const render::UniformAllocation per_object =
                                    frame_context.AllocateUniform(per_object_data);
                                if (!per_object.IsValid())
                                {
                                    continue;
                                }
                                const std::vector<graphics::ResourceBinding> draw_bindings{
                                    graphics::UniformBufferBinding{
                                        0, 0, per_pass.buffer, per_pass.offset, per_pass.range},
                                    graphics::UniformBufferBinding{
                                        0, 1, per_object.buffer, per_object.offset, per_object.range},
                                };
                                const render::FrameMaterialBinding material_binding =
                                    frame_context.CreateMaterialBinding(
                                        materials, resource_resolver, proxy.material, draw_bindings);
                                if (!frame_context.IsMaterialBindingCurrent(material_binding))
                                {
                                    continue;
                                }
                                const bool expects_bindless =
                                    materials.GetInstanceTemplate(proxy.material) == material_template &&
                                    bindless_enabled;
                                if (material_binding.uses_bindless_textures != expects_bindless)
                                {
                                    throw std::runtime_error(
                                        "material draw binding mode did not match backend capability");
                                }
                                recorder->BindPipeline(material_binding.pipeline);
                                recorder->BindMesh(proxy.mesh);
                                recorder->BindResourceBindings(material_binding.pipeline,
                                                               material_binding.descriptor_set);
                                recorder->DrawIndexed();
                            }
                        }
                        recorder->EndRenderTarget();
                        frame_context.End();
                        ++frame_number;
                    }
                }
                rhi->EndFrame();
                if (api == GraphicsAPIType::GRAPHICS_API_OPENGL)
                {
                    window->SwapBuffers();
                }
                ++rendered_frames;
            }
            if (!gameplay_world.DestroyActor(gameplay_actor_handle))
            {
                throw std::runtime_error("failed to destroy gameplay smoke actor");
            }
            gameplay_world.Tick(kDemoDeltaSeconds);
            source_registry.Drain(render_world, resolve_gameplay_source);
            render_world.ApplyPendingCommands();
            if (render_world.Snapshot().size() != 4U)
            {
                throw std::runtime_error("gameplay smoke mesh was not destroyed from RenderWorld");
            }
            // The smoke path owns RHI resources directly, so it must establish
            // the same retirement boundary as RenderSystem before releasing them.
            render_world.Clear();
            source_registry.Clear(render_world);
            materials.DestroyInstance(first_material_instance);
            materials.DestroyInstance(second_material_instance);
            materials.DestroyInstance(first_bound_material_instance);
            materials.DestroyInstance(second_bound_material_instance);
            materials.DestroyTemplate(material_template);
            materials.DestroyTemplate(bound_material_template);
            rhi->WaitIdle();
            for (render::FrameContext &frame_context : frame_contexts)
            {
                frame_context.Cleanup();
            }
            rhi->DestroyBufferResource(dedicated_smoke_buffer);
            rhi->DestroyRenderTarget(scene_target);
            rhi->DestroyMesh(demo_resources.mesh);
            rhi->DestroyPipelineResource(secondary_pipeline);
            resource_resolver.Cleanup();
            rhi->Cleanup();
            window->Cleanup();
            return true;
        }
        catch (const std::exception &e)
        {
            std::cout << e.what() << std::endl;
            return false;
        }
    }

    void RHIExample()
    {
        (void)RunRHI(GraphicsAPIType::GRAPHICS_API_VULKAN, 0, false);
    }

    bool RunGraphicsSmokeSuite(uint32_t frames_per_api)
    {
        return RunRHI(GraphicsAPIType::GRAPHICS_API_VULKAN, frames_per_api, true) &&
               RunRHI(GraphicsAPIType::GRAPHICS_API_OPENGL, frames_per_api, true);
    }
}
