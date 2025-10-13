#include "opengl_backend.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "log/logger.h"
#include "config/path.h"
#include "common/mesh_manager.h"
#include "common/texture_manager.h"
#include "common/sampler_manager.h"
#include "common/shader_manager.h"
#include "opengl_pipeline.h"
#include "common/pipeline_desc.h"
#include "tool/assimp_model_loader.h"
#include "opengl_mesh.h"
namespace kpengine::graphics
{
    OpenglBackend::OpenglBackend() : mesh_manager_(std::make_unique<MeshManager>()),
                                     texture_manager_(std::make_unique<TextureManager>()),
                                     sampler_manager_(std::make_unique<SamplerManager>()),
                                     shader_manager_(std::make_unique<ShaderManager>()),
                                     model_loader_(std::make_unique<AssimpModelLoader>())
    {
        context_.backend = this;
    }

    void OpenglBackend::Initialize()
    {
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        {
            KP_LOG("OpenglBackendLog", LOG_LEVEL_ERROR, "Failed to load OpenGL Loader");
            std::runtime_error("Failed to load OpenGL Loader");
        }

        CreatePipeline();
        CreateMeshes();
    }
    void OpenglBackend::BeginFrame()
    {
        glClearColor(0.f, 0.f, 0.f, 1.f);
    }
    void OpenglBackend::EndFrame()
    {
    }

    void OpenglBackend::Present()
    {
    }

    void OpenglBackend::Cleanup()
    {
        pipeline_->Destroy();
    }

    BufferHandle OpenglBackend::CreateVertexBuffer(const void *data, size_t size)
    {
        GLuint vbo{};
        glGenBuffers(1, &vbo);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);

        return {vbo, 0};
    }

    BufferHandle OpenglBackend::CreateIndexBuffer(const void *data, size_t size)
    {
        GLuint ebo{};
        glGenBuffers(1, &ebo);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, size, data, GL_STATIC_DRAW);

        return {ebo, 0};
    }

    bool OpenglBackend::DestroyBufferResource(BufferHandle handle)
    {
        glDeleteBuffers(1, &handle.id);
        return true;
    }

    void OpenglBackend::CreatePipeline()
    {
        PipelineDesc pipeline_settings{};
        std::string shader_path = GetShaderDirectory();
        ShaderHandle vert_handle = shader_manager_->CreateShader<GraphicsAPIType::GRAPHICS_API_OPENGL>(ShaderType::SHADER_TYPE_VERTEX, shader_path + "simple_triangle.vert");
        Shader *vert_shader = shader_manager_->GetShader(vert_handle);
        ShaderHandle frag_handle = shader_manager_->CreateShader<GraphicsAPIType::GRAPHICS_API_OPENGL>(ShaderType::SHADER_TYPE_FRAGMENT, shader_path + "simple_triangle.frag");
        Shader *frag_shader = shader_manager_->GetShader(frag_handle);

        pipeline_settings.vert_shader = vert_shader;
        pipeline_settings.frag_shader = frag_shader;

        pipeline_settings.binding_descs =
            {
                {0, sizeof(Vertex), 0}};

        pipeline_settings.attri_descs = {
            {0, 0, VertexFormat::VERTEX_FORMAT_THREE_FLOATS, offsetof(Vertex, position)},
            {1, 0, VertexFormat::VERTEX_FORMAT_FOUR_FLOATS, offsetof(Vertex, tex_coord)}};

        pipeline_ = std::make_unique<OpenglPipeline>();
        pipeline_->Initialize(pipeline_settings);
    }

    void OpenglBackend::CreateMeshes()
    {
        std::string model_path = GetModelDirectory() + "sphere/sphere.obj";
        MeshData data{};
        model_loader_->Load(model_path, data);
        GraphicsContext context;
        context.native = static_cast<void*>(&context_);
        context.type = GraphicsAPIType::GRAPHICS_API_OPENGL;
        MeshHandle mesh_handle = mesh_manager_->CreateMesh(context, data);
        Mesh* mesh = mesh_manager_->GetMesh(mesh_handle);
    } 

    void OpenglBackend::CreateUniformBuffers()
    {
        glGenBuffers(1, &camera_ubo_);
        glBindBuffer(GL_UNIFORM_BUFFER, camera_ubo_);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(UniformBufferObject), nullptr, GL_DYNAMIC_DRAW);
        glBindBufferBase(GL_UNIFORM_BUFFER, 0, camera_ubo_);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    void OpenglBackend::UpdateUniformBuffers()
    {
        glBindBuffer(GL_UNIFORM_BUFFER, camera_ubo_);
        UniformBufferObject ubo_data;
        glBufferSubData(camera_ubo_, 0 ,sizeof(UniformBufferObject), &ubo_data);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }
}