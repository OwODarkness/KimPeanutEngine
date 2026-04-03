#include "opengl_backend.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <chrono>
#include "log/logger.h"
#include "config/path.h"
#include "common/mesh_manager.h"
#include "common/texture_manager.h"
#include "common/sampler_manager.h"
#include "common/shader_manager.h"
#include "opengl_pipeline.h"
#include "common/pipeline_types.h"
#include "asset/asset_manager.h"
#include "asset/model.h"
#include "asset/mesh.h"
#include "asset/texture.h"
#include "opengl_mesh.h"
#include "opengl_enum.h"
#include "opengl_texture.h"
#include "opengl_sampler.h"
#include "opengl_descriptorset.h"

namespace kpengine::graphics
{
    OpenglBackend::OpenglBackend() : mesh_manager_(std::make_unique<MeshManager>()),
                                     texture_manager_(std::make_unique<TextureManager>()),
                                     sampler_manager_(std::make_unique<SamplerManager>()),
                                     shader_manager_(std::make_unique<ShaderManager>())

    {
        context_.backend = this;
    }

    OpenglBackend::~OpenglBackend() = default;

    void OpenglBackend::Initialize()
    {
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        {
            KP_LOG("OpenglBackendLog", LOG_LEVEL_ERROR, "Failed to load OpenGL Loader");
            throw std::runtime_error("Failed to load OpenGL Loader");
        }

        glfwGetWindowSize(window_, &width_, &height_);
        CreatePipeline();
        CreateUniformBuffers();
        CreateTextures();
        CreateDescriptorSets();
        CreateMeshes();
    }
    void OpenglBackend::BeginFrame()
    {
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    void OpenglBackend::EndFrame()
    {
        Present();
    }

    void OpenglBackend::Present()
    {
        pipeline_->Bind();

        UpdateUniformBuffers();

        Mesh *mesh_entity = mesh_manager_->GetMesh(mesh_handle);
        MeshResource resource = mesh_entity->GetMeshHandle();
        const OpenglMeshResource *mesh_resource = static_cast<const OpenglMeshResource *>(resource.native);

        glBindVertexArray(pipeline_->vao);

        for (const VertexBindingDesc binding_desc : pipeline_->binding_descs_)
        {
            glBindVertexBuffer(binding_desc.binding, mesh_resource->vbo, 0, binding_desc.stride);
            glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, mesh_resource->ebo);
            for (size_t i = 0; i < mesh_resource->sections.size(); i++)
            {
                glDrawElements(pipeline_->primitive_topology_type_, mesh_resource->sections[i].index_count, GL_UNSIGNED_INT, (void *)(mesh_resource->sections[i].index_start * sizeof(uint32_t)));
            }
        }
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
            {1, 0, VertexFormat::VERTEX_FORMAT_TWO_FLOATS, offsetof(Vertex, tex_coord)}};

        pipeline_settings.descriptor_binding_descs = {
            {{0, 1, DescriptorType::DESCRIPTOR_TYPE_UNIFORM, ShaderStage::SHADER_STAGE_VERTEX},
             {1, 1, DescriptorType::DESCRIPTOR_TYPE_UNIFORM, ShaderStage::SHADER_STAGE_VERTEX},
             {2, 1, DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER, ShaderStage::SHADER_STAGE_FRAGMENT}},
        };

        pipeline_ = std::make_unique<OpenglPipeline>();
        pipeline_->Initialize(pipeline_settings);
    }

    void OpenglBackend::CreateMeshes()
    {
        std::string model_path = GetModelDirectory() + "sphere/sphere.obj";
        asset::AssetID model_id = asset::AssetManager::GetInstance().LoadSync(model_path);
        std::shared_ptr<asset::ModelResource> model_ptr = asset::AssetManager::GetInstance().GetResource<asset::ModelResource>(model_id);
        if (model_ptr)
        {
            asset::AssetID mesh_id = model_ptr->GetData(asset::ModelGeometryType::KPMG_Mesh);
            std::shared_ptr<asset::MeshResource> mesh_ptr = asset::AssetManager::GetInstance().GetResource<asset::MeshResource>(mesh_id);
            if (mesh_ptr)
            {
                GraphicsContext context;
                context.native = &context_;
                context.type = GraphicsAPIType::GRAPHICS_API_OPENGL;

                mesh_handle = mesh_manager_->CreateMesh(context, *mesh_ptr->data);
            }
        }
        asset::AssetManager::GetInstance().UnRegisterAsset(model_id);
        asset::AssetManager::GetInstance().UnRegisterAsset(model_ptr->GetData(asset::ModelGeometryType::KPMG_Mesh));
    }

    void OpenglBackend::CreateUniformBuffers()
    {
        ubos_.resize(2);
        glGenBuffers(2, ubos_.data());
        glBindBuffer(GL_UNIFORM_BUFFER, ubos_[0]);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(PerPassData), nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, ubos_[1]);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(PerObjectData), nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }

    void OpenglBackend::CreateTextures()
    {
        GraphicsContext context = CreateGraphicsContext();
        std::string texture_path = GetTextureDirectory() + "wallpaper.jpg";
        asset::AssetID id = asset::AssetManager::GetInstance().LoadSync(texture_path);
        auto texture_ptr = asset::AssetManager::GetInstance().GetResource<asset::TextureResource>(id);
        if (texture_ptr == nullptr)
        {
            return;
        }
        TextureData &texture_data = *(texture_ptr->resource);

        TextureSettings texture_settings{};
        texture_settings.mip_levels = static_cast<uint32_t>(std::floor(std::log2(std::max(texture_data.width, texture_data.height)))) + 1;
        texture_settings.format = TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
        texture_settings.usage = TextureUsage::TEXTURE_USAGE_TRANSFER_DST | TextureUsage::TEXTURE_USAGE_TRANSFER_SRC | TextureUsage::TEXTURE_USAGE_SAMPLE;
        texture_settings.sample_count = 1;
        texture_handle = texture_manager_->CreateTexture(context, texture_data, texture_settings);

        SamplerSettings sampler_settings{};
        sampler_settings.address_mode_u = SamplerAddressMode::SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_settings.address_mode_v = SamplerAddressMode::SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_settings.address_mode_w = SamplerAddressMode::SAMPLER_ADDRESS_MODE_REPEAT;
        sampler_settings.enable_anisotropy = true;
        sampler_settings.mag_filter = SamplerFilterType::SAMPLER_FILTER_LINEAR;
        sampler_settings.min_filter = SamplerFilterType::SAMPLER_FILTER_LINEAR;
        sampler_settings.mip_lod_bias = 0.f;
        sampler_settings.min_lod = 0.f;
        sampler_settings.max_lod = 0.f;

        sampler_handle = sampler_manager_->CreateSampler(context, sampler_settings);
        asset::AssetManager::GetInstance().UnRegisterAsset(id);
    }

    GraphicsContext OpenglBackend::CreateGraphicsContext()
    {
        GraphicsContext context;
        context.native = &context_;
        context.type = GraphicsAPIType::GRAPHICS_API_OPENGL;
        return context;
    }

    void OpenglBackend::CreateDescriptorSets()
    {
        descriptor_set = std::make_unique<OpenglDescriptorSet>();
        const auto &bindings = pipeline_->descriptor_binding_descs_[0];

        for (const auto &binding : bindings)
        {
            if (binding.descriptor_type == DescriptorType::DESCRIPTOR_TYPE_UNIFORM)
            {
                descriptor_set->SetUniformBuffer(binding.binding, ubos_[binding.binding]);
            }
            else if (binding.descriptor_type == DescriptorType::DESCRIPTOR_TYPE_COMBINE_IMAGE_SAMPLER)
            {
                Texture *texture = texture_manager_->GetTexture(texture_handle);
                OpenglTextureResource texture_resource = ConvertToOpenglTextureResource(texture->GetTextueHandle());
                Sampler *sampler = sampler_manager_->GetSampler(sampler_handle);
                OpenglSamplerResource sampler_resource = ConvertToOpenglSamplerResource(sampler->GetSampleHandle());

                descriptor_set->SetCombinedImageSampler(binding.binding, texture_resource.image, sampler_resource.sampler);
            }
        }
        descriptor_set->Bind();
    }

    void OpenglBackend::UpdateUniformBuffers()
    {
        static auto start_time = std::chrono::high_resolution_clock::now();
        auto current_time = std::chrono::high_resolution_clock::now();
        float time = std::chrono::duration<float, std::chrono::seconds::period>(current_time - start_time).count();

        Vector3f camera = {0.f, 0.f, 2.f};
        Vector3f target = {0.f, 0.f, 0.f};
        Vector3f dir = target - camera;

        PerPassData per_pass_data{};
        per_pass_data.camera_data.view = Matrix4f::MakeCameraMatrix(camera, dir, {0.f, 1.f, 0.f}).Transpose();
        float aspect = width_ / (float)height_;
        per_pass_data.camera_data.proj = Matrix4f::MakePerProjMatrix(math::DegreeToRadian(45.f), aspect, 0.1f, 10.f).Transpose();
        per_pass_data.camera_data.proj[1][1] *= -1.0;

        Transform3f model{};
        model.scale_ = {0.5f, 0.5f, 0.5f};
        model.rotator_.pitch_ = time * 90.f;

        PerObjectData per_object_data{};
        per_object_data.model = Matrix4f::MakeTransformMatrix(model).Transpose();

        glBindBuffer(GL_UNIFORM_BUFFER, ubos_[0]);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(PerPassData), &per_pass_data);
        glBindBuffer(GL_UNIFORM_BUFFER, ubos_[1]);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(PerObjectData), &per_object_data);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
    }
}