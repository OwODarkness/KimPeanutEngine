
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

#include "runtime/window/glfw_window_system.h"
#include "runtime/input/input_system.h"
#include "runtime/input/input_context.h"
#include "runtime/render/render_camera.h"
#include "runtime/render/render_capture_service_internal.h"
#include "runtime/image_io/image_io.h"
#include "runtime/screenshot/runtime_screenshot_service.h"
#include "runtime/render/frame_context.h"
#include "runtime/render/render_resource_resolver.h"
#include "runtime/render/renderer_frame_targets.h"
#include "runtime/render/material/material_system.h"
#include "runtime/render/material/material_asset_resolver.h"
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

    static constexpr uint8_t kPngSignature[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};

    static bool HasPngSignature(const std::vector<uint8_t> &bytes)
    {
        return bytes.size() >= sizeof(kPngSignature) &&
               std::equal(kPngSignature, kPngSignature + sizeof(kPngSignature), bytes.begin());
    }

    static uint32_t ReadBigEndian32(const std::vector<uint8_t> &bytes, size_t offset)
    {
        return (static_cast<uint32_t>(bytes[offset]) << 24) |
               (static_cast<uint32_t>(bytes[offset + 1]) << 16) |
               (static_cast<uint32_t>(bytes[offset + 2]) << 8) |
               static_cast<uint32_t>(bytes[offset + 3]);
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
            // The scene target mirrors the old single-color+depth defaults
            // explicitly now that RenderTargetDesc defaults to "no attachments".
            graphics::RenderTargetDesc render_target_desc{};
            render_target_desc.width = static_cast<uint32_t>(window_create_info.width);
            render_target_desc.height = static_cast<uint32_t>(window_create_info.height);
            render_target_desc.color_attachments = {{graphics::RenderTargetColorAttachment{}}};
            render_target_desc.depth = graphics::RenderTargetDepthAttachment{};
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
            // Shared frame body: the main loop and the post-loop capture drain
            // both record the same scene so a pending readback has work to wait on.
            const auto render_one_frame = [&]()
            {
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
                                        materials, resource_resolver, proxy.material, draw_bindings,
                                        render::MaterialPass::Scene);
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
            };
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
                render_one_frame();
                ++rendered_frames;
            }
            // C4 smoke verification: the agent-facing screenshot export must
            // complete with a deterministic validation PNG on both backends,
            // and that PNG must hold a real capture of the post-resize frame.
            if (max_frames != 0)
            {
                const std::string api_name =
                    api == GraphicsAPIType::GRAPHICS_API_VULKAN ? "vulkan" : "opengl";
                const std::string requested_path =
                    "save/screenshots/validation/graphics-smoke-" + api_name + ".png";
                // The export service never overwrites; drop a stale artifact so
                // every run reproduces the canonical validation filename.
                std::error_code remove_error;
                std::filesystem::remove(requested_path, remove_error);

                render::RenderCaptureService capture_service(
                    rhi->GetRenderTargetReadback(),
                    [scene_target] { return scene_target; },
                    [&frame_number] { return frame_number; });
                runtime::RuntimeScreenshotService screenshot_service(capture_service);
                runtime::ScreenshotResult screenshot_result;
                bool screenshot_finished = false;
                if (!screenshot_service.RequestScreenshot(
                        {{render::CaptureView::SceneColor}, requested_path},
                        [&screenshot_result, &screenshot_finished](
                            runtime::ScreenshotResult result)
                        {
                            screenshot_result = std::move(result);
                            screenshot_finished = true;
                        }))
                {
                    throw std::runtime_error("smoke screenshot request was rejected");
                }
                constexpr uint32_t kMaxCaptureDrainFrames = 16;
                uint32_t drain_frames = 0;
                while (!screenshot_finished && drain_frames < kMaxCaptureDrainFrames)
                {
                    render_one_frame();
                    ++drain_frames;
                }
                if (!screenshot_finished || !screenshot_result.IsSuccess())
                {
                    throw std::runtime_error("smoke screenshot did not complete: " +
                                             screenshot_result.diagnostic);
                }
                if (std::filesystem::path{screenshot_result.output_path}.filename() !=
                    ("graphics-smoke-" + api_name + ".png"))
                {
                    throw std::runtime_error("smoke screenshot exported to an unexpected path: " +
                                             screenshot_result.output_path);
                }

                // Validate the written PNG: signature and IHDR extent first,
                // then a decode round trip for tightly packed non-empty pixels.
                std::vector<uint8_t> file_bytes;
                {
                    std::ifstream file(screenshot_result.output_path, std::ios::binary);
                    if (!file)
                    {
                        throw std::runtime_error("smoke screenshot PNG could not be opened");
                    }
                    file.seekg(0, std::ios::end);
                    const std::streamoff file_size = file.tellg();
                    file.seekg(0, std::ios::beg);
                    file_bytes.resize(static_cast<size_t>(file_size));
                    file.read(reinterpret_cast<char *>(file_bytes.data()),
                              static_cast<std::streamsize>(file_size));
                }
                if (!HasPngSignature(file_bytes))
                {
                    throw std::runtime_error("smoke screenshot PNG has an invalid signature");
                }
                if (file_bytes.size() < 24 || ReadBigEndian32(file_bytes, 8) != 13 ||
                    file_bytes[12] != 'I' || file_bytes[13] != 'H' ||
                    file_bytes[14] != 'D' || file_bytes[15] != 'R')
                {
                    throw std::runtime_error("smoke screenshot PNG has no valid IHDR chunk");
                }
                if (ReadBigEndian32(file_bytes, 16) !=
                        static_cast<uint32_t>(window_create_info.width) ||
                    ReadBigEndian32(file_bytes, 20) !=
                        static_cast<uint32_t>(window_create_info.height))
                {
                    throw std::runtime_error("smoke screenshot PNG extent did not match the target");
                }

                const image_io::ImageDecodeResult decoded =
                    image_io::DecodeImageFile(screenshot_result.output_path);
                if (!decoded.result.success || !decoded.image.IsValid() ||
                    decoded.image.format != image_io::ImagePixelFormat::Rgba8)
                {
                    throw std::runtime_error("smoke screenshot PNG failed to decode: " +
                                             decoded.result.diagnostic);
                }
                if (decoded.image.pixels.size() != decoded.image.ExpectedByteCount())
                {
                    throw std::runtime_error("smoke screenshot PNG returned unpadded pixels");
                }
                const std::vector<uint8_t> &pixels = decoded.image.pixels;
                const uint8_t first_byte = pixels.front();
                const bool has_varied_pixels = std::any_of(
                    pixels.begin() + 1, pixels.end(),
                    [first_byte](uint8_t byte) { return byte != first_byte; });
                if (!has_varied_pixels)
                {
                    throw std::runtime_error("smoke screenshot PNG is a uniform image");
                }
            }
            // D2 cross-backend contract proof: multiple named color attachments,
            // an attachment sampled by a later pass, and a depth-only target
            // lifecycle. Runs on the smoke path so both APIs are exercised.
            if (max_frames != 0)
            {
                const std::string api_name =
                    api == GraphicsAPIType::GRAPHICS_API_VULKAN ? "vulkan" : "opengl";
                const asset::AssetID multi_program_id =
                    LoadShaderProgram(resource_pipeline, "multi_output.shader");
                const asset::AssetID sample_program_id =
                    LoadShaderProgram(resource_pipeline, "sample_color.shader");
                const asset::AssetID depth_program_id =
                    LoadShaderProgram(resource_pipeline, "depth_only.shader");
                auto multi_program = asset::AssetManager::GetInstance().GetResource<asset::ShaderProgramResource>(multi_program_id);
                auto sample_program = asset::AssetManager::GetInstance().GetResource<asset::ShaderProgramResource>(sample_program_id);
                auto depth_program = asset::AssetManager::GetInstance().GetResource<asset::ShaderProgramResource>(depth_program_id);
                const auto multi_vertex = multi_program ? multi_program->GetShader(
                    ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL) : nullptr;
                const auto multi_fragment = multi_program ? multi_program->GetShader(
                    ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL) : nullptr;
                const auto sample_vertex = sample_program ? sample_program->GetShader(
                    ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL) : nullptr;
                const auto sample_fragment = sample_program ? sample_program->GetShader(
                    ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL) : nullptr;
                const auto depth_vertex = depth_program ? depth_program->GetShader(
                    ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL) : nullptr;
                const auto depth_fragment = depth_program ? depth_program->GetShader(
                    ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL) : nullptr;
                if (!multi_vertex || !multi_fragment || !sample_vertex || !sample_fragment ||
                    !depth_vertex || !depth_fragment || !multi_vertex->data || !multi_fragment->data ||
                    !sample_vertex->data || !sample_fragment->data ||
                    !depth_vertex->data || !depth_fragment->data)
                {
                    throw std::runtime_error("D2 smoke shaders failed to compile");
                }

                const uint32_t target_width = static_cast<uint32_t>(window_create_info.width);
                const uint32_t target_height = static_cast<uint32_t>(window_create_info.height);

                // Multi-attachment target: RGBA8 + RGBA16F color, D32 depth.
                graphics::RenderTargetDesc multi_desc{};
                multi_desc.width = target_width;
                multi_desc.height = target_height;
                multi_desc.color_attachments = {
                    {graphics::RenderTargetColorAttachment{TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM}},
                    {graphics::RenderTargetColorAttachment{TextureFormat::TEXTURE_FORMAT_RGBA16F}},
                };
                multi_desc.depth = graphics::RenderTargetDepthAttachment{};
                multi_desc.depth->shader_readable = true;
                const graphics::RenderTargetHandle multi_target = rhi->CreateRenderTarget(multi_desc);
                // Readback-capturable RGBA8_SRGB output the sample pass fills.
                graphics::RenderTargetDesc output_desc{};
                output_desc.width = target_width;
                output_desc.height = target_height;
                output_desc.color_attachments = {{graphics::RenderTargetColorAttachment{}}};
                const graphics::RenderTargetHandle output_target = rhi->CreateRenderTarget(output_desc);
                // Depth-only target: no color attachments at all.
                graphics::RenderTargetDesc depth_only_desc{};
                depth_only_desc.width = 512;
                depth_only_desc.height = 512;
                depth_only_desc.depth = graphics::RenderTargetDepthAttachment{};
                const graphics::RenderTargetHandle depth_only_target = rhi->CreateRenderTarget(depth_only_desc);
                if (!multi_target.IsValid() || !output_target.IsValid() || !depth_only_target.IsValid() ||
                    !rhi->GetRenderTargetColorAttachment(multi_target, 0).IsValid() ||
                    !rhi->GetRenderTargetColorAttachment(multi_target, 1).IsValid() ||
                    !rhi->GetRenderTargetDepthAttachment(multi_target).IsValid() ||
                    !rhi->GetRenderTargetDepthAttachment(depth_only_target).IsValid() ||
                    rhi->GetRenderTargetDepthAttachment(output_target).IsValid() ||
                    !rhi->GetRenderTargetSampledDepthAttachment(multi_target).IsValid() ||
                    rhi->GetRenderTargetSampledDepthAttachment(depth_only_target).IsValid())
                {
                    throw std::runtime_error("failed to create D2 multi-attachment / depth-only targets");
                }

                // The common CCW winding must remain front-facing after each
                // backend translates it; keep back-face culling enabled as a
                // cross-API regression check.
                graphics::RasterState fullscreen_raster_state = raster_state;
                fullscreen_raster_state.cull_mode = graphics::CullMode::CULL_MODE_BACK;
                graphics::PipelineDesc multi_pipeline_desc{};
                multi_pipeline_desc.vert_shader = sample_vertex->data.get();
                multi_pipeline_desc.frag_shader = multi_fragment->data.get();
                multi_pipeline_desc.binding_descs = {{0, sizeof(data::Vertex), false}};
                multi_pipeline_desc.attri_descs = {
                    {0, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS, offsetof(data::Vertex, position)},
                    {1, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS, offsetof(data::Vertex, tex_coord)},
                };
                multi_pipeline_desc.raster_state = fullscreen_raster_state;
                multi_pipeline_desc.color_attachment_formats = {
                    TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM,
                    TextureFormat::TEXTURE_FORMAT_RGBA16F};
                multi_pipeline_desc.depth_attachment_format = TextureFormat::TEXTURE_FORMAT_D32;

                graphics::PipelineDesc sample_pipeline_desc{};
                sample_pipeline_desc.vert_shader = sample_vertex->data.get();
                sample_pipeline_desc.frag_shader = sample_fragment->data.get();
                sample_pipeline_desc.binding_descs = {{0, sizeof(data::Vertex), false}};
                sample_pipeline_desc.attri_descs = {
                    {0, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS, offsetof(data::Vertex, position)},
                    {1, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS, offsetof(data::Vertex, tex_coord)},
                };
                sample_pipeline_desc.descriptor_binding_descs = {
                    {{2, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER, ShaderStage::SHADER_STAGE_FRAGMENT}},
                };
                sample_pipeline_desc.raster_state = fullscreen_raster_state;
                sample_pipeline_desc.color_attachment_formats = {TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB};
                sample_pipeline_desc.depth_attachment_format = TextureFormat::TEXTURE_FORMAT_UNKNOW;

                const graphics::PipelineHandle multi_pipeline =
                    rhi->CreatePipelineResource(multi_pipeline_desc);
                const graphics::PipelineHandle sample_pipeline =
                    rhi->CreatePipelineResource(sample_pipeline_desc);
                // Depth-only pipeline: no color attachments; drives the camera +
                // model uniforms like the scene passes and writes depth only.
                graphics::PipelineDesc depth_pipeline_desc{};
                depth_pipeline_desc.vert_shader = depth_vertex->data.get();
                depth_pipeline_desc.frag_shader = depth_fragment->data.get();
                depth_pipeline_desc.binding_descs = {{0, sizeof(data::Vertex), false}};
                depth_pipeline_desc.attri_descs = {
                    {0, 0, graphics::VertexFormat::VERTEX_FORMAT_THREE_FLOATS, offsetof(data::Vertex, position)},
                    {1, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS, offsetof(data::Vertex, tex_coord)},
                };
                depth_pipeline_desc.descriptor_binding_descs = {
                    {{0, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM, ShaderStage::SHADER_STAGE_VERTEX},
                     {1, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM, ShaderStage::SHADER_STAGE_VERTEX}},
                };
                depth_pipeline_desc.raster_state = raster_state;
                depth_pipeline_desc.depth_attachment_format = TextureFormat::TEXTURE_FORMAT_D32;
                const graphics::PipelineHandle depth_only_pipeline =
                    rhi->CreatePipelineResource(depth_pipeline_desc);
                const graphics::SamplerHandle sample_sampler = rhi->CreateSampler(graphics::SamplerSettings{});
                if (!multi_pipeline.IsValid() || !sample_pipeline.IsValid() ||
                    !depth_only_pipeline.IsValid() || !sample_sampler.IsValid())
                {
                    throw std::runtime_error("failed to create D2 smoke pipelines / sampler");
                }

                // Fullscreen triangle mesh (position + uv) for the sample pass.
                data::MeshData fullscreen_mesh_data{};
                data::Vertex quad_v0{}, quad_v1{}, quad_v2{};
                quad_v0.position = {-1.0f, -1.0f, 0.0f};
                quad_v0.tex_coord = {0.0f, 0.0f};
                quad_v1.position = {3.0f, -1.0f, 0.0f};
                quad_v1.tex_coord = {2.0f, 0.0f};
                quad_v2.position = {-1.0f, 3.0f, 0.0f};
                quad_v2.tex_coord = {0.0f, 2.0f};
                fullscreen_mesh_data.vertices = {quad_v0, quad_v1, quad_v2};
                fullscreen_mesh_data.indices = {0, 1, 2};
                fullscreen_mesh_data.sections = {{0, 3, 0}};
                const graphics::MeshHandle fullscreen_mesh = rhi->CreateMesh(fullscreen_mesh_data);
                if (!fullscreen_mesh.IsValid())
                {
                    throw std::runtime_error("failed to create D2 fullscreen mesh");
                }
                const graphics::DescriptorSetHandle sample_bindings = rhi->CreateResourceBindingSet(
                    sample_pipeline,
                    {0, {graphics::SampledTextureBinding{
                             0, 2, rhi->GetRenderTargetColorAttachment(multi_target, 0),
                             sample_sampler}}});
                if (!sample_bindings.IsValid())
                {
                    throw std::runtime_error("failed to create D2 sample descriptor set");
                }

                // Per-frame D2 recording: multi-attachment pass, then sample the
                // multi attachment 0 into the output, then the depth-only pass.
                // Mirrors render_one_frame so pending readbacks have work to wait on.
                const auto render_d2_frame = [&]()
                {
                    rhi->BeginFrame();
                    if (rhi->GetCommandRecorder())
                    {
                        const uint32_t frame_index = rhi->GetCurrentFrameIndex();
                        if (frame_index < frame_contexts.size())
                        {
                            render::FrameContext &frame_context = frame_contexts[frame_index];
                            frame_context.Begin(frame_index,
                                               {frame_number, elapsed_seconds, kDemoDeltaSeconds},
                                               rhi->GetRenderExtent());
                            graphics::CommandRecorder *recorder = rhi->GetCommandRecorder();

                            camera.SetAspect(static_cast<float>(target_width) /
                                             static_cast<float>(target_height));
                            const render::CameraData camera_data = camera.GetCameraData();
                            graphics::PerPassData per_pass_data{};
                            per_pass_data.camera_data.view = camera_data.view;
                            per_pass_data.camera_data.proj = camera_data.proj;
                            const render::UniformAllocation d2_pass =
                                frame_context.AllocateUniform(per_pass_data);
                            graphics::PerObjectData per_object_data{};
                            per_object_data.model = Matrix4f::MakeTransformMatrix(Transform3f{}).Transpose();
                            const render::UniformAllocation d2_object =
                                frame_context.AllocateUniform(per_object_data);
                            if (!d2_pass.IsValid() || !d2_object.IsValid())
                            {
                                throw std::runtime_error("D2 uniform allocation failed");
                            }

                            recorder->BeginRenderTarget(multi_target);
                            recorder->BindPipeline(multi_pipeline);
                            recorder->BindMesh(fullscreen_mesh);
                            recorder->DrawIndexed();
                            recorder->EndRenderTarget();

                            recorder->BeginRenderTarget(output_target);
                            recorder->BindPipeline(sample_pipeline);
                            recorder->BindMesh(fullscreen_mesh);
                            recorder->BindResourceBindings(sample_pipeline, sample_bindings);
                            recorder->DrawIndexed();
                            recorder->EndRenderTarget();

                            recorder->BeginRenderTarget(depth_only_target);
                            const graphics::DescriptorSetHandle depth_bindings =
                                frame_context.AllocateResourceBindingSet(
                                    depth_only_pipeline,
                                    {0,
                                     {graphics::UniformBufferBinding{0, 0, d2_pass.buffer, d2_pass.offset, d2_pass.range},
                                      graphics::UniformBufferBinding{0, 1, d2_object.buffer, d2_object.offset, d2_object.range}}});
                            if (!depth_bindings.IsValid())
                            {
                                throw std::runtime_error("D2 depth-only pass binding failed");
                            }
                            recorder->BindPipeline(depth_only_pipeline);
                            recorder->BindMesh(demo_resources.mesh);
                            recorder->BindResourceBindings(depth_only_pipeline, depth_bindings);
                            recorder->DrawIndexed();
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
                };

                // Depth-only lifecycle proof: a resize lands while the depth-only
                // target is live and a frame records into all three targets.
                window->SetWindowSize(800, 600);
                window->resize_event_dispatcher_.Dispatch({800, 600});
                render_d2_frame();

                // Capture the output target the sample pass filled.
                const std::string d2_path =
                    "save/screenshots/validation/graphics-smoke-d2-" + api_name + ".png";
                std::error_code d2_remove_error;
                std::filesystem::remove(d2_path, d2_remove_error);
                render::RenderCaptureService d2_capture_service(
                    rhi->GetRenderTargetReadback(),
                    [output_target] { return output_target; },
                    [&frame_number] { return frame_number; });
                runtime::RuntimeScreenshotService d2_screenshot_service(d2_capture_service);
                runtime::ScreenshotResult d2_screenshot_result;
                bool d2_screenshot_finished = false;
                if (!d2_screenshot_service.RequestScreenshot(
                        {{render::CaptureView::SceneColor}, d2_path},
                        [&d2_screenshot_result, &d2_screenshot_finished](
                            runtime::ScreenshotResult result)
                        {
                            d2_screenshot_result = std::move(result);
                            d2_screenshot_finished = true;
                        }))
                {
                    throw std::runtime_error("D2 smoke screenshot request was rejected");
                }
                constexpr uint32_t kMaxCaptureDrainFrames = 16;
                uint32_t d2_drain_frames = 0;
                while (!d2_screenshot_finished && d2_drain_frames < kMaxCaptureDrainFrames)
                {
                    render_d2_frame();
                    ++d2_drain_frames;
                }
                if (!d2_screenshot_finished || !d2_screenshot_result.IsSuccess())
                {
                    throw std::runtime_error("D2 smoke screenshot did not complete: " +
                                             d2_screenshot_result.diagnostic);
                }
                std::vector<uint8_t> d2_file_bytes;
                {
                    std::ifstream file(d2_screenshot_result.output_path, std::ios::binary);
                    if (!file)
                    {
                        throw std::runtime_error("D2 smoke screenshot PNG could not be opened");
                    }
                    file.seekg(0, std::ios::end);
                    const std::streamoff d2_file_size = file.tellg();
                    file.seekg(0, std::ios::beg);
                    d2_file_bytes.resize(static_cast<size_t>(d2_file_size));
                    file.read(reinterpret_cast<char *>(d2_file_bytes.data()),
                              static_cast<std::streamsize>(d2_file_size));
                }
                if (!HasPngSignature(d2_file_bytes))
                {
                    throw std::runtime_error("D2 smoke screenshot PNG has an invalid signature");
                }
                const image_io::ImageDecodeResult d2_decoded =
                    image_io::DecodeImageFile(d2_screenshot_result.output_path);
                if (!d2_decoded.result.success || !d2_decoded.image.IsValid() ||
                    d2_decoded.image.format != image_io::ImagePixelFormat::Rgba8)
                {
                    throw std::runtime_error("D2 smoke screenshot PNG failed to decode: " +
                                             d2_decoded.result.diagnostic);
                }
                const std::vector<uint8_t> &d2_pixels = d2_decoded.image.pixels;
                const uint8_t d2_first_byte = d2_pixels.front();
                const bool d2_has_varied_pixels = std::any_of(
                    d2_pixels.begin() + 1, d2_pixels.end(),
                    [d2_first_byte](uint8_t byte) { return byte != d2_first_byte; });
                if (!d2_has_varied_pixels)
                {
                    throw std::runtime_error("D2 smoke screenshot PNG is a uniform image");
                }
                // The sampled attachment is a UV ramp; the output must show both
                // red and green intensity so the transfer is real, not cleared.
                bool saw_red = false;
                bool saw_green = false;
                for (size_t i = 0; i + 3 < d2_pixels.size(); i += 4)
                {
                    if (d2_pixels[i] > 64) { saw_red = true; }
                    if (d2_pixels[i + 1] > 64) { saw_green = true; }
                }
                if (!saw_red || !saw_green)
                {
                    throw std::runtime_error("D2 sampled output lost the multi-attachment content");
                }

                rhi->WaitIdle();
                rhi->DestroyResourceBindingSet(sample_bindings);
                rhi->DestroySampler(sample_sampler);
                rhi->DestroyPipelineResource(sample_pipeline);
                rhi->DestroyPipelineResource(multi_pipeline);
                rhi->DestroyPipelineResource(depth_only_pipeline);
                rhi->DestroyMesh(fullscreen_mesh);
                rhi->DestroyRenderTarget(depth_only_target);
                rhi->DestroyRenderTarget(output_target);
                rhi->DestroyRenderTarget(multi_target);
            }
            // D3-D5.1 deferred-PBR proof: the material writes the G-buffer, the
            // diagnostic conversion writes linear RGBA16F SceneHdr, and a
            // distinct tone-map pass produces the captured RGBA8 SceneColor.
            if (max_frames != 0)
            {
                const uint32_t target_width = static_cast<uint32_t>(window_create_info.width);
                const uint32_t target_height = static_cast<uint32_t>(window_create_info.height);
                const std::string api_name =
                    api == GraphicsAPIType::GRAPHICS_API_VULKAN ? "vulkan" : "opengl";
                render::RendererFrameTargets named_targets;
                named_targets.Initialize(*rhi, target_width, target_height);
                const render::RenderTarget *const named_scene_color =
                    named_targets.GetTarget(render::RenderTargetName::SceneColor);
                const render::RenderTarget *const named_gbuffer =
                    named_targets.GetTarget(render::RenderTargetName::GBuffer);
                const render::RenderTarget *const named_shadow =
                    named_targets.GetTarget(render::RenderTargetName::DirectionalShadow);
                const render::RenderTarget *const named_scene_hdr =
                    named_targets.GetTarget(render::RenderTargetName::SceneHdr);
                if (!named_scene_color || !named_scene_color->IsValid() ||
                    !named_gbuffer || !named_gbuffer->IsValid() ||
                    !named_shadow || !named_shadow->IsValid() ||
                    !named_shadow->GetSampledDepthTexture().IsValid() ||
                    !named_scene_hdr || !named_scene_hdr->IsValid() ||
                    named_scene_hdr->GetColorAttachmentCount() != 1 ||
                    !named_scene_hdr->GetColorAttachmentTexture(0).IsValid())
                {
                    throw std::runtime_error("D5.1 named frame target set is incomplete");
                }
                named_targets.Cleanup();
                render::MaterialAssetResolver d3_material_resolver(materials);
                const asset::AssetID rock_material_id = asset::AssetManager::GetInstance().LoadSync(
                    GetAssetDirectory() + "material/rock_pbr.material");
                render::MaterialInstanceHandle rock_instance;
                const render::MaterialResolution rock_resolution =
                    d3_material_resolver.Resolve(rock_material_id, rock_instance);
                if (rock_resolution.state != render::MaterialResourceState::Ready ||
                    !rock_instance.IsValid())
                {
                    throw std::runtime_error("D3 rock material failed to resolve: " +
                                             rock_resolution.diagnostic);
                }
                const render::MaterialTemplateHandle rock_template =
                    materials.GetInstanceTemplate(rock_instance);
                const graphics::PipelineHandle gbuffer_pipeline =
                    resource_resolver.FindMaterialPipeline(rock_template,
                                                           render::MaterialPass::GBuffer);
                if (!gbuffer_pipeline.IsValid())
                {
                    throw std::runtime_error("D3 GBuffer pipeline was not created for the rock material");
                }

                // Rock mesh: 56-byte data::Vertex (pos/normal/uv/tangent/bitangent).
                const asset::AssetID rock_model_id = asset::AssetManager::GetInstance().LoadSync(
                    GetModelDirectory() + "rock1-bl/rock2.obj");
                auto rock_model = asset::AssetManager::GetInstance().GetResource<asset::ModelResource>(rock_model_id);
                auto rock_mesh = rock_model ? rock_model->GetMesh() : nullptr;
                if (!rock_mesh || !rock_mesh->data)
                {
                    throw std::runtime_error("D3 rock mesh failed to load");
                }
                const asset::AssetID rock_mesh_id = rock_model->GetData(asset::ModelGeometryType::KPMG_Mesh);
                const graphics::MeshHandle rock_mesh_handle =
                    resource_resolver.GetOrCreateMesh(rock_mesh_id, *rock_mesh->data);
                if (!rock_mesh_handle.IsValid())
                {
                    throw std::runtime_error("D3 rock mesh resource creation failed");
                }

                // GBuffer target: 3 color (albedo/normal/material) + D32, matching
                // the engine's RendererFrameTargets::BuildDesc encodings.
                graphics::RenderTargetDesc gbuffer_desc{};
                gbuffer_desc.width = target_width;
                gbuffer_desc.height = target_height;
                gbuffer_desc.color_attachments = {
                    {graphics::RenderTargetColorAttachment{
                         TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM,
                         graphics::RenderTargetLoadOp::Clear,
                         graphics::RenderTargetStoreOp::Store,
                         {0.f, 0.f, 0.f, 0.f}}},
                    {graphics::RenderTargetColorAttachment{
                         TextureFormat::TEXTURE_FORMAT_RGBA16F,
                         graphics::RenderTargetLoadOp::Clear,
                         graphics::RenderTargetStoreOp::Store,
                         {0.f, 0.f, 1.f, 0.f}}},
                    {graphics::RenderTargetColorAttachment{
                         TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM,
                         graphics::RenderTargetLoadOp::Clear,
                         graphics::RenderTargetStoreOp::Store,
                         {0.f, 1.f, 1.f, 0.f}}},
                };
                gbuffer_desc.depth = graphics::RenderTargetDepthAttachment{
                    TextureFormat::TEXTURE_FORMAT_D32, graphics::RenderTargetLoadOp::Clear,
                    graphics::RenderTargetStoreOp::Store, 1.0f, 0, true};
                const graphics::RenderTargetHandle gbuffer_target = rhi->CreateRenderTarget(gbuffer_desc);
                graphics::RenderTargetDesc d3_output_desc{};
                d3_output_desc.width = target_width;
                d3_output_desc.height = target_height;
                d3_output_desc.color_attachments = {{graphics::RenderTargetColorAttachment{}}};
                const graphics::RenderTargetHandle d3_output_target = rhi->CreateRenderTarget(d3_output_desc);
                graphics::RenderTargetDesc d5_hdr_desc{};
                d5_hdr_desc.width = target_width;
                d5_hdr_desc.height = target_height;
                d5_hdr_desc.color_attachments = {{graphics::RenderTargetColorAttachment{
                    TextureFormat::TEXTURE_FORMAT_RGBA16F,
                    graphics::RenderTargetLoadOp::Clear,
                    graphics::RenderTargetStoreOp::Store,
                    {0.f, 0.f, 0.f, 1.f}}}};
                const graphics::RenderTargetHandle d5_hdr_target = rhi->CreateRenderTarget(d5_hdr_desc);
                graphics::RenderTargetDesc d4_shadow_desc{};
                d4_shadow_desc.width = 1024;
                d4_shadow_desc.height = 1024;
                d4_shadow_desc.depth = graphics::RenderTargetDepthAttachment{
                    TextureFormat::TEXTURE_FORMAT_D32, graphics::RenderTargetLoadOp::Clear,
                    graphics::RenderTargetStoreOp::Store, 1.0f, 0, true};
                const graphics::RenderTargetHandle d4_shadow_target = rhi->CreateRenderTarget(d4_shadow_desc);
                if (!gbuffer_target.IsValid() || !d3_output_target.IsValid() ||
                    !d4_shadow_target.IsValid() || !d5_hdr_target.IsValid() ||
                    !rhi->GetRenderTargetColorAttachment(gbuffer_target, 0).IsValid() ||
                    !rhi->GetRenderTargetColorAttachment(gbuffer_target, 1).IsValid() ||
                    !rhi->GetRenderTargetColorAttachment(gbuffer_target, 2).IsValid() ||
                    !rhi->GetRenderTargetSampledDepthAttachment(gbuffer_target).IsValid() ||
                    !rhi->GetRenderTargetSampledDepthAttachment(d4_shadow_target).IsValid())
                {
                    throw std::runtime_error("failed to create D3 GBuffer / output targets");
                }

                // Deferred directional-lighting pipeline + shared fullscreen mesh.
                const asset::AssetID deferred_lighting_program_id =
                    LoadShaderProgram(resource_pipeline, "deferred_lighting.shader");
                const asset::AssetID directional_shadow_program_id =
                    LoadShaderProgram(resource_pipeline, "directional_shadow_depth.shader");
                const asset::AssetID tone_map_program_id =
                    LoadShaderProgram(resource_pipeline, "tone_map.shader");
                auto deferred_lighting_program = asset::AssetManager::GetInstance().GetResource<asset::ShaderProgramResource>(deferred_lighting_program_id);
                auto directional_shadow_program = asset::AssetManager::GetInstance().GetResource<asset::ShaderProgramResource>(directional_shadow_program_id);
                auto tone_map_program = asset::AssetManager::GetInstance().GetResource<asset::ShaderProgramResource>(tone_map_program_id);
                const auto deferred_lighting_vertex = deferred_lighting_program ? deferred_lighting_program->GetShader(
                    ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL) : nullptr;
                const auto deferred_lighting_fragment = deferred_lighting_program ? deferred_lighting_program->GetShader(
                    ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL) : nullptr;
                const auto directional_shadow_vertex = directional_shadow_program ? directional_shadow_program->GetShader(
                    ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL) : nullptr;
                const auto directional_shadow_fragment = directional_shadow_program ? directional_shadow_program->GetShader(
                    ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL) : nullptr;
                const auto tone_map_vertex = tone_map_program ? tone_map_program->GetShader(
                    ShaderStage::SHADER_STAGE_VERTEX, ShaderFormat::SHADER_FORMAT_GLSL) : nullptr;
                const auto tone_map_fragment = tone_map_program ? tone_map_program->GetShader(
                    ShaderStage::SHADER_STAGE_FRAGMENT, ShaderFormat::SHADER_FORMAT_GLSL) : nullptr;
                if (!deferred_lighting_vertex || !deferred_lighting_fragment ||
                    !deferred_lighting_vertex->data || !deferred_lighting_fragment->data ||
                    !directional_shadow_vertex || !directional_shadow_fragment ||
                    !directional_shadow_vertex->data || !directional_shadow_fragment->data ||
                    !tone_map_vertex || !tone_map_fragment || !tone_map_vertex->data ||
                    !tone_map_fragment->data)
                {
                    throw std::runtime_error("D5.2 deferred-lighting shaders failed to compile");
                }
                graphics::PipelineDesc deferred_lighting_pipeline_desc{};
                deferred_lighting_pipeline_desc.vert_shader = deferred_lighting_vertex->data.get();
                deferred_lighting_pipeline_desc.frag_shader = deferred_lighting_fragment->data.get();
                deferred_lighting_pipeline_desc.binding_descs = {{0, sizeof(data::Vertex), false}};
                deferred_lighting_pipeline_desc.attri_descs = {
                    {0, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS, offsetof(data::Vertex, position)},
                    {1, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS, offsetof(data::Vertex, tex_coord)},
                };
                deferred_lighting_pipeline_desc.descriptor_binding_descs = {
                    {{0, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER, ShaderStage::SHADER_STAGE_FRAGMENT},
                     {1, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER, ShaderStage::SHADER_STAGE_FRAGMENT},
                     {2, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER, ShaderStage::SHADER_STAGE_FRAGMENT},
                     {3, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER, ShaderStage::SHADER_STAGE_FRAGMENT},
                     {4, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM, ShaderStage::SHADER_STAGE_FRAGMENT},
                     {5, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM, ShaderStage::SHADER_STAGE_FRAGMENT}},
                };
                deferred_lighting_pipeline_desc.raster_state.cull_mode = graphics::CullMode::CULL_MODE_BACK;
                deferred_lighting_pipeline_desc.raster_state.front_face =
                    graphics::FrontFace::FRONT_FACE_COUNTER_CLOCKWISE;
                deferred_lighting_pipeline_desc.color_attachment_formats = {TextureFormat::TEXTURE_FORMAT_RGBA16F};
                deferred_lighting_pipeline_desc.depth_attachment_format = TextureFormat::TEXTURE_FORMAT_UNKNOW;
                const graphics::PipelineHandle deferred_lighting_pipeline =
                    rhi->CreatePipelineResource(deferred_lighting_pipeline_desc);
                graphics::PipelineDesc directional_shadow_pipeline_desc{};
                directional_shadow_pipeline_desc.vert_shader = directional_shadow_vertex->data.get();
                directional_shadow_pipeline_desc.frag_shader = directional_shadow_fragment->data.get();
                directional_shadow_pipeline_desc.binding_descs = {{0, sizeof(data::Vertex), false}};
                directional_shadow_pipeline_desc.attri_descs = {
                    {0, 0, graphics::VertexFormat::VERTEX_FORMAT_THREE_FLOATS, offsetof(data::Vertex, position)},
                };
                directional_shadow_pipeline_desc.descriptor_binding_descs = {
                    {{0, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM, ShaderStage::SHADER_STAGE_VERTEX},
                     {1, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_UNIFORM, ShaderStage::SHADER_STAGE_VERTEX}},
                };
                directional_shadow_pipeline_desc.raster_state.cull_mode = graphics::CullMode::CULL_MODE_NONE;
                directional_shadow_pipeline_desc.depth_attachment_format = TextureFormat::TEXTURE_FORMAT_D32;
                const graphics::PipelineHandle directional_shadow_pipeline =
                    rhi->CreatePipelineResource(directional_shadow_pipeline_desc);
                graphics::PipelineDesc tone_map_pipeline_desc{};
                tone_map_pipeline_desc.vert_shader = tone_map_vertex->data.get();
                tone_map_pipeline_desc.frag_shader = tone_map_fragment->data.get();
                tone_map_pipeline_desc.binding_descs = {{0, sizeof(data::Vertex), false}};
                tone_map_pipeline_desc.attri_descs = {
                    {0, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS, offsetof(data::Vertex, position)},
                    {1, 0, graphics::VertexFormat::VERTEX_FORMAT_TWO_FLOATS, offsetof(data::Vertex, tex_coord)},
                };
                tone_map_pipeline_desc.descriptor_binding_descs = {
                    {{2, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER,
                      ShaderStage::SHADER_STAGE_FRAGMENT}},
                };
                tone_map_pipeline_desc.raster_state.cull_mode = graphics::CullMode::CULL_MODE_BACK;
                tone_map_pipeline_desc.raster_state.front_face =
                    graphics::FrontFace::FRONT_FACE_COUNTER_CLOCKWISE;
                tone_map_pipeline_desc.color_attachment_formats = {
                    TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB};
                tone_map_pipeline_desc.depth_attachment_format = TextureFormat::TEXTURE_FORMAT_UNKNOW;
                const graphics::PipelineHandle tone_map_pipeline =
                    rhi->CreatePipelineResource(tone_map_pipeline_desc);
                data::MeshData debug_mesh_data{};
                data::Vertex debug_v0{}, debug_v1{}, debug_v2{};
                debug_v0.position = {-1.0f, -1.0f, 0.0f};
                debug_v0.tex_coord = {0.0f, 0.0f};
                debug_v1.position = {3.0f, -1.0f, 0.0f};
                debug_v1.tex_coord = {2.0f, 0.0f};
                debug_v2.position = {-1.0f, 3.0f, 0.0f};
                debug_v2.tex_coord = {0.0f, 2.0f};
                debug_mesh_data.vertices = {debug_v0, debug_v1, debug_v2};
                debug_mesh_data.indices = {0, 1, 2};
                debug_mesh_data.sections = {{0, 3, 0}};
                const graphics::MeshHandle debug_mesh = rhi->CreateMesh(debug_mesh_data);
                const graphics::SamplerHandle debug_sampler = rhi->CreateSampler(graphics::SamplerSettings{});
                if (!deferred_lighting_pipeline.IsValid() || !directional_shadow_pipeline.IsValid() ||
                    !tone_map_pipeline.IsValid() ||
                    !debug_mesh.IsValid() || !debug_sampler.IsValid())
                {
                    throw std::runtime_error("failed to create D5.2 deferred-lighting resources");
                }

                // Frame the ~165-unit rock: pull the camera back + extend the far
                // plane (the smoke camera default far=10 culls it entirely).
                camera.SetPosition({0.f, 0.f, 300.f});
                camera.SetNearPlane(1.f);
                camera.SetFarPlane(2000.f);

                render::LightGpuFrameData d5_lighting_data{};
                d5_lighting_data.header.light_count = 1;
                render::LightGpuData &d5_directional_light = d5_lighting_data.lights[0];
                d5_directional_light.color_intensity = {1.0f, 0.95f, 0.9f, 4.0f};
                d5_directional_light.direction_inner_cone = {0.0f, -0.5f, -1.0f, 0.0f};
                d5_directional_light.type = static_cast<uint32_t>(render::LightGpuType::Directional);
                d5_directional_light.enabled = 1;

                const auto render_d3_frame = [&]()
                {
                    rhi->BeginFrame();
                    if (rhi->GetCommandRecorder())
                    {
                        const uint32_t frame_index = rhi->GetCurrentFrameIndex();
                        if (frame_index < frame_contexts.size())
                        {
                            render::FrameContext &frame_context = frame_contexts[frame_index];
                            frame_context.Begin(frame_index,
                                               {frame_number, elapsed_seconds, kDemoDeltaSeconds},
                                               rhi->GetRenderExtent());
                            graphics::CommandRecorder *recorder = rhi->GetCommandRecorder();

                            camera.SetAspect(static_cast<float>(target_width) /
                                             static_cast<float>(target_height));
                            const render::CameraData camera_data = camera.GetCameraData();
                            graphics::PerPassData per_pass_data{};
                            per_pass_data.camera_data.view = camera_data.view;
                            per_pass_data.camera_data.proj = camera_data.proj;
                            const render::UniformAllocation d3_pass =
                                frame_context.AllocateUniform(per_pass_data);
                            graphics::PerObjectData per_object_data{};
                            per_object_data.model = Matrix4f::MakeTransformMatrix(Transform3f{}).Transpose();
                            const render::UniformAllocation d3_object =
                                frame_context.AllocateUniform(per_object_data);
                            const render::FrameLightingBinding d5_lighting_binding =
                                frame_context.CreateLightingBinding(d5_lighting_data);
                            render::DeferredLightingGpuData d5_lighting_constants{};
                            d5_lighting_constants.inverse_view_projection =
                                camera.GetViewProjectionMatrix().Inverse().Transpose();
                            d5_lighting_constants.camera_world_position = {0.0f, 0.0f, 300.0f, 1.0f};
                            const render::UniformAllocation d5_constants =
                                frame_context.AllocateUniform(d5_lighting_constants);
                            if (!d3_pass.IsValid() || !d3_object.IsValid() ||
                                !d5_lighting_binding.IsValid() || !d5_constants.IsValid())
                            {
                                throw std::runtime_error("D5.2 frame uniform allocation failed");
                            }

                            // D4 contract smoke: depth-only, sampled target uses the
                            // same opaque mesh and per-frame matrix bindings.
                            recorder->BeginRenderTarget(d4_shadow_target);
                            const graphics::DescriptorSetHandle directional_shadow_bindings =
                                frame_context.AllocateResourceBindingSet(
                                    directional_shadow_pipeline,
                                    {0,
                                     {graphics::UniformBufferBinding{0, 0, d3_pass.buffer, d3_pass.offset, d3_pass.range},
                                      graphics::UniformBufferBinding{0, 1, d3_object.buffer, d3_object.offset, d3_object.range}}});
                            if (!directional_shadow_bindings.IsValid())
                            {
                                throw std::runtime_error("D4 directional shadow binding failed");
                            }
                            recorder->BindPipeline(directional_shadow_pipeline);
                            recorder->BindMesh(rock_mesh_handle);
                            recorder->BindResourceBindings(directional_shadow_pipeline,
                                                           directional_shadow_bindings);
                            recorder->DrawIndexed();
                            recorder->EndRenderTarget();

                            recorder->BeginRenderTarget(gbuffer_target);
                            const std::vector<graphics::ResourceBinding> gbuffer_draw_bindings{
                                graphics::UniformBufferBinding{0, 0, d3_pass.buffer, d3_pass.offset, d3_pass.range},
                                graphics::UniformBufferBinding{0, 1, d3_object.buffer, d3_object.offset, d3_object.range},
                            };
                            const render::FrameMaterialBinding gbuffer_binding =
                                frame_context.CreateMaterialBinding(
                                    materials, resource_resolver, rock_instance, gbuffer_draw_bindings,
                                    render::MaterialPass::GBuffer);
                            if (!frame_context.IsMaterialBindingCurrent(gbuffer_binding))
                            {
                                throw std::runtime_error("D3 GBuffer material binding failed");
                            }
                            recorder->BindPipeline(gbuffer_binding.pipeline);
                            recorder->BindMesh(rock_mesh_handle);
                            recorder->BindResourceBindings(gbuffer_binding.pipeline,
                                                           gbuffer_binding.descriptor_set);
                            recorder->DrawIndexed();
                            recorder->EndRenderTarget();

                            recorder->BeginRenderTarget(d5_hdr_target);
                            const graphics::DescriptorSetHandle deferred_lighting_bindings =
                                frame_context.AllocateResourceBindingSet(
                                    deferred_lighting_pipeline,
                                    {0,
                                     {graphics::SampledTextureBinding{
                                          0, 0, rhi->GetRenderTargetColorAttachment(gbuffer_target, 0),
                                          debug_sampler},
                                      graphics::SampledTextureBinding{
                                          0, 1, rhi->GetRenderTargetColorAttachment(gbuffer_target, 1),
                                          debug_sampler},
                                      graphics::SampledTextureBinding{
                                          0, 2, rhi->GetRenderTargetColorAttachment(gbuffer_target, 2),
                                          debug_sampler},
                                      graphics::SampledTextureBinding{
                                          0, 3, rhi->GetRenderTargetSampledDepthAttachment(gbuffer_target),
                                          debug_sampler},
                                      d5_lighting_binding.GetResourceBinding(),
                                      graphics::UniformBufferBinding{
                                          0, 5, d5_constants.buffer, d5_constants.offset,
                                          d5_constants.range}}});
                            if (!deferred_lighting_bindings.IsValid())
                            {
                                throw std::runtime_error("D5.2 deferred-lighting binding failed");
                            }
                            recorder->BindPipeline(deferred_lighting_pipeline);
                            recorder->BindMesh(debug_mesh);
                            recorder->BindResourceBindings(deferred_lighting_pipeline,
                                                           deferred_lighting_bindings);
                            recorder->DrawIndexed();
                            recorder->EndRenderTarget();

                            recorder->BeginRenderTarget(d3_output_target);
                            const graphics::DescriptorSetHandle tone_map_bindings =
                                frame_context.AllocateResourceBindingSet(
                                    tone_map_pipeline,
                                    {0,
                                     {graphics::SampledTextureBinding{
                                         0, 2,
                                         rhi->GetRenderTargetColorAttachment(d5_hdr_target, 0),
                                         debug_sampler}}});
                            if (!tone_map_bindings.IsValid())
                            {
                                throw std::runtime_error("D5.1 tone-map binding failed");
                            }
                            recorder->BindPipeline(tone_map_pipeline);
                            recorder->BindMesh(debug_mesh);
                            recorder->BindResourceBindings(tone_map_pipeline, tone_map_bindings);
                            recorder->DrawIndexed();
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
                };

                render_d3_frame();

                const std::string d3_path =
                    "save/screenshots/validation/graphics-smoke-d5-" + api_name + ".png";
                std::error_code d3_remove_error;
                std::filesystem::remove(d3_path, d3_remove_error);
                render::RenderCaptureService d3_capture_service(
                    rhi->GetRenderTargetReadback(),
                    [d3_output_target] { return d3_output_target; },
                    [&frame_number] { return frame_number; });
                runtime::RuntimeScreenshotService d3_screenshot_service(d3_capture_service);
                runtime::ScreenshotResult d3_screenshot_result;
                bool d3_screenshot_finished = false;
                if (!d3_screenshot_service.RequestScreenshot(
                        {{render::CaptureView::SceneColor}, d3_path},
                        [&d3_screenshot_result, &d3_screenshot_finished](
                            runtime::ScreenshotResult result)
                        {
                            d3_screenshot_result = std::move(result);
                            d3_screenshot_finished = true;
                        }))
                {
                    throw std::runtime_error("D3 smoke screenshot request was rejected");
                }
                constexpr uint32_t kMaxCaptureDrainFrames = 16;
                uint32_t d3_drain_frames = 0;
                while (!d3_screenshot_finished && d3_drain_frames < kMaxCaptureDrainFrames)
                {
                    render_d3_frame();
                    ++d3_drain_frames;
                }
                if (!d3_screenshot_finished || !d3_screenshot_result.IsSuccess())
                {
                    throw std::runtime_error("D3 smoke screenshot did not complete: " +
                                             d3_screenshot_result.diagnostic);
                }
                const image_io::ImageDecodeResult d3_decoded =
                    image_io::DecodeImageFile(d3_screenshot_result.output_path);
                if (!d3_decoded.result.success || !d3_decoded.image.IsValid() ||
                    d3_decoded.image.format != image_io::ImagePixelFormat::Rgba8)
                {
                    throw std::runtime_error("D3 smoke screenshot PNG failed to decode: " +
                                             d3_decoded.result.diagnostic);
                }
                const std::vector<uint8_t> &d3_pixels = d3_decoded.image.pixels;
                const uint8_t d3_first_byte = d3_pixels.front();
                const bool d3_has_varied_pixels = std::any_of(
                    d3_pixels.begin() + 1, d3_pixels.end(),
                    [d3_first_byte](uint8_t byte) { return byte != d3_first_byte; });
                if (!d3_has_varied_pixels)
                {
                    throw std::runtime_error("D3 smoke screenshot PNG is a uniform image");
                }
                // The ambient floor stays below this threshold, so bright pixels
                // demonstrate that the directional light reached the BRDF path.
                bool d3_saw_rock = false;
                for (size_t i = 0; i + 2 < d3_pixels.size(); i += 4)
                {
                    if (d3_pixels[i] > 32 || d3_pixels[i + 1] > 32 || d3_pixels[i + 2] > 32)
                    {
                        d3_saw_rock = true;
                        break;
                    }
                }
                if (!d3_saw_rock)
                {
                    throw std::runtime_error("D5.2 deferred lighting did not illuminate the rock");
                }

                rhi->WaitIdle();
                d3_material_resolver.Clear();
                rhi->DestroySampler(debug_sampler);
                rhi->DestroyMesh(debug_mesh);
                rhi->DestroyPipelineResource(tone_map_pipeline);
                rhi->DestroyPipelineResource(deferred_lighting_pipeline);
                rhi->DestroyPipelineResource(directional_shadow_pipeline);
                rhi->DestroyRenderTarget(d4_shadow_target);
                rhi->DestroyRenderTarget(d5_hdr_target);
                rhi->DestroyRenderTarget(d3_output_target);
                rhi->DestroyRenderTarget(gbuffer_target);
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
