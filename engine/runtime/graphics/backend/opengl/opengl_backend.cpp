#include "opengl_backend.h"
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <chrono>
#include "log/logger.h"
#include "config/path.h"
#include "common/mesh_manager.h"
#include "common/texture_manager.h"
#include "common/sampler_manager.h"
#include "opengl_pipeline.h"
#include "opengl_pipeline_manager.h"
#include "common/pipeline_types.h"
#include "asset/asset_manager.h"
#include "asset/model.h"
#include "asset/mesh.h"
#include "asset/texture.h"
#include "opengl_mesh.h"
#include "common/mesh.h"
#include "opengl_enum.h"
#include "opengl_texture.h"
#include "opengl_sampler.h"
#include "opengl_descriptorset.h"

namespace kpengine::graphics
{
    OpenglBackend::OpenglBackend() : mesh_manager_(std::make_unique<MeshManager>()),
                                     texture_manager_(std::make_unique<TextureManager>()),
                                     sampler_manager_(std::make_unique<SamplerManager>()),
                                     pipeline_manager_(std::make_unique<OpenglPipelineManager>())
    {
        context_.backend = this;
    }

    OpenglBackend::~OpenglBackend() = default;

    void OpenglBackend::Initialize(WindowHandle native_window)
    {
        if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
        {
            KP_LOG("OpenglBackendLog", LOG_LEVEL_ERROR, "Failed to load OpenGL Loader");
            throw std::runtime_error("Failed to load OpenGL Loader");
        }

        GLFWwindow *window = static_cast<GLFWwindow *>(native_window);
        glfwGetWindowSize(window, &width_, &height_);
    }
    void OpenglBackend::BeginFrame()
    {
        frame_active_ = true;
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }
    void OpenglBackend::EndFrame()
    {
        frame_active_ = false;
    }

    CommandRecorder *OpenglBackend::GetCommandRecorder()
    {
        return frame_active_ ? static_cast<CommandRecorder *>(this) : nullptr;
    }

    void OpenglBackend::BindPipeline(PipelineHandle pipeline)
    {
        OpenglPipeline *resource = pipeline_manager_->GetPipelineResource(pipeline);
        if (resource)
        {
            resource->Bind();
            glBindVertexArray(resource->vao);
            recorded_pipeline_ = pipeline;
        }
    }

    void OpenglBackend::BindMesh(MeshHandle mesh)
    {
        Mesh *mesh_object = mesh_manager_->GetMesh(mesh);
        if (!mesh_object)
        {
            return;
        }
        const auto *mesh_resource = static_cast<const OpenglMeshResource *>(
            mesh_object->GetMeshHandle().native);
        if (!mesh_resource)
        {
            return;
        }
        OpenglPipeline *pipeline = pipeline_manager_->GetPipelineResource(recorded_pipeline_);
        if (!pipeline)
        {
            return;
        }
        glVertexArrayVertexBuffer(pipeline->vao, 0, mesh_resource->vbo, 0, sizeof(Vertex));
        glVertexArrayElementBuffer(pipeline->vao, mesh_resource->ebo);
        glBindVertexArray(pipeline->vao);
        recorded_mesh_ = mesh;
        recorded_index_count_ = mesh_resource->sections.empty()
            ? 0u : static_cast<uint32_t>(mesh_resource->sections[0].index_count);
    }

    void OpenglBackend::BindResourceBindings(PipelineHandle pipeline,
                                             DescriptorSetHandle bindings)
    {
        BindResourceBindingSet(pipeline, bindings);
    }

    void OpenglBackend::SetViewport(const Viewport &viewport)
    {
        glViewport(static_cast<GLint>(viewport.x), static_cast<GLint>(viewport.y),
                   static_cast<GLsizei>(viewport.width), static_cast<GLsizei>(viewport.height));
    }

    void OpenglBackend::SetScissor(const Scissor &scissor)
    {
        glScissor(scissor.x, scissor.y, static_cast<GLsizei>(scissor.width),
                  static_cast<GLsizei>(scissor.height));
    }

    void OpenglBackend::DrawIndexed(uint32_t index_count, uint32_t instance_count,
                                    uint32_t first_index, int32_t vertex_offset,
                                    uint32_t first_instance)
    {
        (void)vertex_offset;
        (void)first_instance;
        if (index_count == 0)
        {
            index_count = recorded_index_count_;
        }
        if (index_count != 0)
        {
            glDrawElementsInstanced(GL_TRIANGLES, static_cast<GLsizei>(index_count),
                                    GL_UNSIGNED_INT,
                                    reinterpret_cast<const void *>(first_index * sizeof(uint32_t)),
                                    static_cast<GLsizei>(instance_count));
        }
    }

    BufferHandle OpenglBackend::CreateUniformBuffer(uint32_t size)
    {
        GLuint buffer = 0;
        glGenBuffers(1, &buffer);
        glBindBuffer(GL_UNIFORM_BUFFER, buffer);
        glBufferData(GL_UNIFORM_BUFFER, size, nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
        mapped_uniform_buffers_[buffer] = {buffer, std::vector<uint8_t>(size)};
        return {buffer, 0};
    }

    void *OpenglBackend::MapUniformBuffer(BufferHandle handle, size_t size)
    {
        auto it = mapped_uniform_buffers_.find(handle.id);
        if (it == mapped_uniform_buffers_.end())
        {
            return nullptr;
        }
        it->second.data.resize(size);
        return it->second.data.data();
    }

    size_t OpenglBackend::GetUniformBufferAlignment() const
    {
        GLint alignment = 1;
        glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &alignment);
        return alignment > 0 ? static_cast<size_t>(alignment) : 1;
    }

    Extent2D OpenglBackend::GetRenderExtent() const
    {
        return {static_cast<uint32_t>(width_), static_cast<uint32_t>(height_)};
    }

    void OpenglBackend::Cleanup()
    {
        pipeline_manager_->DestroyAll();
        resource_binding_sets_.clear();
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
        mapped_uniform_buffers_.erase(handle.id);
        glDeleteBuffers(1, &handle.id);
        return true;
    }

    PipelineHandle OpenglBackend::CreatePipelineResource(const PipelineDesc &pipeline_desc)
    {
        return pipeline_manager_->CreatePipelineResource(pipeline_desc);
    }

    bool OpenglBackend::DestroyPipelineResource(PipelineHandle handle)
    {
        return pipeline_manager_->DestroyPipelineResource(handle);
    }

    MeshHandle OpenglBackend::CreateMesh(const data::MeshData &data)
    {
        return mesh_manager_->CreateMesh(CreateGraphicsContext(), data);
    }

    bool OpenglBackend::DestroyMesh(MeshHandle handle)
    {
        return mesh_manager_->DestroyMesh(CreateGraphicsContext(), handle);
    }

    TextureHandle OpenglBackend::CreateTexture(const data::TextureData &data,
                                               const TextureSettings &settings)
    {
        return texture_manager_->CreateTexture(CreateGraphicsContext(), data, settings);
    }

    bool OpenglBackend::DestroyTexture(TextureHandle handle)
    {
        return texture_manager_->DestroyTexture(CreateGraphicsContext(), handle);
    }

    SamplerHandle OpenglBackend::CreateSampler(const SamplerSettings &settings)
    {
        return sampler_manager_->CreateSampler(CreateGraphicsContext(), settings);
    }

    bool OpenglBackend::DestroySampler(SamplerHandle handle)
    {
        return sampler_manager_->DestroySampler(CreateGraphicsContext(), handle);
    }

    DescriptorSetHandle OpenglBackend::CreateResourceBindingSet(
        PipelineHandle pipeline, const ResourceBindingSetDesc &desc)
    {
        (void)pipeline;
        const DescriptorSetHandle handle = resource_binding_set_handles_.Create();
        if (handle.id == resource_binding_sets_.size())
        {
            resource_binding_sets_.emplace_back();
        }

        auto set = std::make_unique<OpenglDescriptorSet>();
        for (const ResourceBinding &binding : desc.bindings)
        {
            std::visit([&](const auto &value) {
                using Binding = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Binding, UniformBufferBinding>)
                {
                    set->SetUniformBuffer(value.binding, value.buffer.id);
                }
                else
                {
                    Texture *texture = texture_manager_->GetTexture(value.texture);
                    Sampler *sampler = sampler_manager_->GetSampler(value.sampler);
                    if (!texture || !sampler)
                    {
                        return;
                    }
                    const OpenglTextureResource texture_resource =
                        ConvertToOpenglTextureResource(texture->GetTextueHandle());
                    const OpenglSamplerResource sampler_resource =
                        ConvertToOpenglSamplerResource(sampler->GetSampleHandle());
                    set->SetCombinedImageSampler(value.binding, texture_resource.image,
                                                 sampler_resource.sampler);
                }
            }, binding);
        }
        resource_binding_sets_[handle.id] = std::move(set);
        return handle;
    }

    bool OpenglBackend::DestroyResourceBindingSet(DescriptorSetHandle handle)
    {
        const uint32_t index = resource_binding_set_handles_.Get(handle);
        if (index >= resource_binding_sets_.size() || !resource_binding_sets_[index])
        {
            return false;
        }
        resource_binding_sets_[index].reset();
        return resource_binding_set_handles_.Destroy(handle);
    }

    void OpenglBackend::BindResourceBindingSet(PipelineHandle pipeline, DescriptorSetHandle handle)
    {
        (void)pipeline;
        const uint32_t index = resource_binding_set_handles_.Get(handle);
        if (index < resource_binding_sets_.size() && resource_binding_sets_[index])
        {
            for (const auto &[id, mapped] : mapped_uniform_buffers_)
            {
                glBindBuffer(GL_UNIFORM_BUFFER, mapped.native);
                glBufferSubData(GL_UNIFORM_BUFFER, 0, mapped.data.size(), mapped.data.data());
            }
            resource_binding_sets_[index]->Bind();
        }
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
        TextureData &texture_data = *(texture_ptr->data);

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
        // Temporary legacy path; descriptor binding moves to the common recorder.
        const auto &bindings = pipeline_manager_->GetPipelineResource(PipelineHandle{})->descriptor_binding_descs_[0];

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
