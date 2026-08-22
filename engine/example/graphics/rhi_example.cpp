
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "runtime/window/glfw_window_system.h"
#include "runtime/input/input_system.h"
#include "runtime/input/input_context.h"
#include "runtime/render/render_scene.h"
#include "runtime/graphics/backend/common/pipeline_types.h"
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
    static std::vector<asset::ShaderPtr> CompileTriangleShaders(GraphicsAPIType api)
    {
        resource::ResourcePipeline pipeline;
        resource::ResourcePipelineContext pipeline_context{api};
        pipeline.Initialize(pipeline_context);

        const std::string path = GetShaderDirectory() + "simple_triangle.shader";
        asset::AssetID id = asset::AssetManager::GetInstance().LoadSync(path);
        if (!id.IsValid())
        {
            throw std::runtime_error("failed to load shader program: " + path);
        }
        auto program = asset::AssetManager::GetInstance().GetResource<asset::ShaderProgramResource>(id);
        if (!program)
        {
            throw std::runtime_error("loaded asset holds no shader program resource");
        }

        // The pipeline writes the baked artifact (byte_code for Vulkan, source
        // for OpenGL) back into each stage's data.
        std::vector<asset::ShaderPtr> shaders;
        for (ShaderStage stage : {ShaderStage::SHADER_STAGE_VERTEX, ShaderStage::SHADER_STAGE_FRAGMENT})
        {
            if (auto shader = program->GetShader(stage, ShaderFormat::SHADER_FORMAT_GLSL))
            {
                shaders.push_back(shader);
            }
        }
        pipeline.ProcessShader(shaders);
        return shaders;
    }

    struct DemoResourceHandles
    {
        graphics::MeshHandle mesh;
        graphics::TextureHandle texture;
        graphics::SamplerHandle sampler;
    };

    static DemoResourceHandles CreateDemoResources(graphics::RenderBackend &backend)
    {
        DemoResourceHandles handles{};

        const asset::AssetID texture_id = asset::AssetManager::GetInstance().LoadSync(
            GetTextureDirectory() + "wallpaper.jpg");
        auto texture = asset::AssetManager::GetInstance().GetResource<asset::TextureResource>(texture_id);
        if (!texture || !texture->data)
        {
            throw std::runtime_error("failed to load demo texture");
        }
        graphics::TextureSettings texture_settings{};
        texture_settings.mip_levels = 1;
        texture_settings.format = texture->data->format;
        texture_settings.usage = graphics::TextureUsage::TEXTURE_USAGE_TRANSFER_DST |
                                 graphics::TextureUsage::TEXTURE_USAGE_SAMPLE;
        handles.texture = backend.CreateTexture(*texture->data, texture_settings);
        handles.sampler = backend.CreateSampler({});
        asset::AssetManager::GetInstance().UnRegisterAsset(texture_id);

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

            std::vector<asset::ShaderPtr> shaders = CompileTriangleShaders(api);
            if (shaders.size() != 2 || !shaders[0]->data || !shaders[1]->data)
            {
                throw std::runtime_error("triangle shaders failed to compile");
            }

            graphics::PipelineDesc pipeline_desc{};
            pipeline_desc.vert_shader = shaders[0]->data.get();
            pipeline_desc.frag_shader = shaders[1]->data.get();
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
                 {2, 1, graphics::DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER, ShaderStage::SHADER_STAGE_FRAGMENT}},
            };

            graphics::RasterState raster_state{};
            raster_state.cull_mode = graphics::CullMode::CULL_MODE_BACK;
            raster_state.front_face = graphics::FrontFace::FRONT_FACE_COUNTER_CLOCKWISE;
            pipeline_desc.raster_state = raster_state;

            std::unique_ptr<graphics::RenderBackend> rhi = graphics::RenderBackend::CreateGraphicsBackEnd(api);
            rhi->BindWindowResize(window->resize_event_dispatcher_);
            rhi->Initialize(window->GetNativeHandle());
            graphics::RenderTargetDesc render_target_desc{};
            render_target_desc.width = static_cast<uint32_t>(window_create_info.width);
            render_target_desc.height = static_cast<uint32_t>(window_create_info.height);
            const graphics::RenderTargetHandle scene_target =
                rhi->CreateRenderTarget(render_target_desc);
            if (!scene_target.IsValid() || !rhi->GetRenderTargetColor(scene_target).IsValid())
            {
                throw std::runtime_error("failed to create scene render target");
            }
            const graphics::PipelineHandle primary_pipeline = rhi->CreatePipelineResource(pipeline_desc);
            const graphics::PipelineHandle secondary_pipeline = rhi->CreatePipelineResource(pipeline_desc);
            if (!primary_pipeline.IsValid() || !secondary_pipeline.IsValid())
            {
                throw std::runtime_error("failed to create graphics pipelines");
            }

            std::vector<render::FrameContext> frame_contexts(rhi->GetFramesInFlight());
            for (render::FrameContext &frame_context : frame_contexts)
            {
                frame_context.Initialize(*rhi, 64 * 1024);
            }

            std::unique_ptr<render::RenderScene> scene;
            DemoResourceHandles demo_resources = CreateDemoResources(*rhi);
            scene = std::make_unique<render::RenderScene>();
            render::RenderSceneInitInfo scene_init_info{};
            scene_init_info.resources.pipeline = primary_pipeline;
            scene_init_info.resources.mesh = demo_resources.mesh;
            scene_init_info.resources.material = {demo_resources.texture, demo_resources.sampler};
            scene->Initialize(scene_init_info);

            uint64_t frame_number = 0;
            float elapsed_seconds = 0.0f;
            constexpr float kDemoDeltaSeconds = 1.0f / 60.0f;
            uint32_t rendered_frames = 0;
            while (!window->ShouldClose() && (max_frames == 0 || rendered_frames < max_frames))
            {
                window->PollEvents();
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
                        scene->Record(frame_context, *recorder);
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
            // The smoke path owns RHI resources directly, so it must establish
            // the same retirement boundary as RenderSystem before releasing them.
            rhi->WaitIdle();
            scene->Cleanup();
            for (render::FrameContext &frame_context : frame_contexts)
            {
                frame_context.Cleanup();
            }
            rhi->DestroyRenderTarget(scene_target);
            rhi->DestroyMesh(demo_resources.mesh);
            rhi->DestroySampler(demo_resources.sampler);
            rhi->DestroyTexture(demo_resources.texture);
            rhi->DestroyPipelineResource(secondary_pipeline);
            rhi->DestroyPipelineResource(primary_pipeline);
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
