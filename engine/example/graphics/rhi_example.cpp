
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "runtime/window/glfw_window_system.h"
#include "runtime/input/input_system.h"
#include "runtime/input/input_context.h"
#include "runtime/graphics/backend/vulkan/vulkan_backend.h"
#include "runtime/render/render_scene.h"
#include "runtime/graphics/backend/common/pipeline_types.h"
#include "runtime/core/data/shader.h"
#include "runtime/core/data/mesh.h"
#include "runtime/core/config/path.h"
#include "runtime/asset/asset_manager.h"
#include "runtime/asset/shader.h"
#include "runtime/asset/shader_program.h"
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

    void RHIExample()
    {
        try
        {
            std::unique_ptr<WindowSystem> window = WindowSystem::CreateWindowSystem(WindowAPIType::WINDOW_API_GLFW);
            WindowCreateInfo window_create_info;
            window_create_info.graphics_api_type = GraphicsAPIType::GRAPHICS_API_VULKAN;
            // window_create_info.graphics_api_type = GraphicsAPIType::GRAPHICS_API_OPENGL;
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

            const GraphicsAPIType api = window_create_info.graphics_api_type;
            const bool is_opengl = api == GraphicsAPIType::GRAPHICS_API_OPENGL;

            std::vector<asset::ShaderPtr> shaders = CompileTriangleShaders(api);
            if (shaders.size() != 2 || !shaders[0]->data || !shaders[1]->data)
            {
                throw std::runtime_error("triangle shaders failed to compile");
            }

            graphics::PipelineDesc pipeline_desc{};
            pipeline_desc.vert_shader = shaders[0]->data.get();
            pipeline_desc.frag_shader = shaders[1]->data.get();

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
            rhi->Initialize(pipeline_desc, window->GetNativeHandle());

            // The demo is the render module's first real scene: for Vulkan it
            // records the frame's draws through the backend's recording API. The
            // OpenGL backend still owns its baked scene for now.
            std::unique_ptr<render::RenderScene> scene;
            if (!is_opengl)
            {
                scene = std::make_unique<render::RenderScene>();
                scene->Initialize(static_cast<graphics::VulkanBackend *>(rhi.get()));
            }

            while (!window->ShouldClose())
            {
                if (is_opengl)
                {
                    window->SwapBuffers();
                }
                window->PollEvents();
                rhi->BeginFrame();
                if (scene)
                {
                    scene->Tick(0.f);
                    scene->Record();
                }
                rhi->EndFrame();
            }
            if (scene)
            {
                scene->Cleanup();
            }
            rhi->Cleanup();
            window->Cleanup();
        }
        catch (std::exception e)
        {
            std::cout << e.what() << std::endl;
        }
    }
}
