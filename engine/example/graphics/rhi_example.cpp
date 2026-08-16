
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
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

namespace kpengine::example
{
    // The example owns the pipeline contract. Shaders arrive as baked bytes — SPIR-V
    // for Vulkan, source text for OpenGL — as data::ShaderData; the RHI never reads
    // shader files itself. (File reading is a stopgap: the render module will source
    // these from the resource pipeline instead of the demo reading disk.)
    static std::vector<char> ReadFileBytes(const std::string &path)
    {
        std::ifstream file(path, std::ios::binary);
        return std::vector<char>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    static std::shared_ptr<data::ShaderData> LoadShaderData(GraphicsAPIType api, ShaderStage stage, const std::string &path)
    {
        auto shader_data = std::make_shared<data::ShaderData>();
        shader_data->api = api;
        shader_data->stage = stage;
        std::vector<char> bytes = ReadFileBytes(path);
        if (api == GraphicsAPIType::GRAPHICS_API_OPENGL)
        {
            shader_data->source.assign(bytes.begin(), bytes.end());
        }
        else
        {
            shader_data->byte_code.resize(bytes.size());
            std::memcpy(shader_data->byte_code.data(), bytes.data(), bytes.size());
        }
        return shader_data;
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
            const std::string shader_dir = is_opengl ? GetShaderDirectory() : GetSPVShaderDirectory();

            std::shared_ptr<data::ShaderData> vert_data = LoadShaderData(
                api, ShaderStage::SHADER_STAGE_VERTEX,
                shader_dir + (is_opengl ? std::string("simple_triangle.vert") : std::string("simple_triangle.vert.spv")));
            std::shared_ptr<data::ShaderData> frag_data = LoadShaderData(
                api, ShaderStage::SHADER_STAGE_FRAGMENT,
                shader_dir + (is_opengl ? std::string("simple_triangle.frag") : std::string("simple_triangle.frag.spv")));

            graphics::PipelineDesc pipeline_desc{};
            pipeline_desc.vert_shader = vert_data.get();
            pipeline_desc.frag_shader = frag_data.get();

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
            rhi->window_ = static_cast<GLFWwindow *>(window->GetNativeHandle());
            rhi->Initialize(pipeline_desc);

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
