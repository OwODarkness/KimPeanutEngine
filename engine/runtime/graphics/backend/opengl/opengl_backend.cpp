#include "opengl_backend.h"
#include <type_traits>
#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include "log/logger.h"
#include "common/mesh_manager.h"
#include "common/texture_manager.h"
#include "common/sampler_manager.h"
#include "opengl_pipeline.h"
#include "opengl_pipeline_manager.h"
#include "common/pipeline_types.h"
#include "common/pipeline_validation.h"
#include "opengl_mesh.h"
#include "common/mesh.h"
#include "opengl_enum.h"
#include "opengl_texture.h"
#include "opengl_sampler.h"
#include "opengl_descriptorset.h"
#include "opengl_bindless_texture_table.h"
#include "opengl_render_target_readback.h"

namespace kpengine::graphics
{
    OpenglBackend::OpenglBackend() : mesh_manager_(std::make_unique<MeshManager>()),
                                     texture_manager_(std::make_unique<TextureManager>()),
                                     sampler_manager_(std::make_unique<SamplerManager>()),
                                     pipeline_manager_(std::make_unique<OpenglPipelineManager>())
    {
        context_.backend = this;
        render_target_readback_ = std::make_unique<OpenglRenderTargetReadback>(
            [this](RenderTargetHandle handle)
            { return GetRenderTargetReadbackSource(handle); });
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
        auto bindless_table = std::make_unique<OpenglBindlessTextureTable>();
        if (bindless_table->Initialize())
        {
            bindless_texture_table_ = std::move(bindless_table);
        }
        InitializeCapabilities();
    }

    void OpenglBackend::InitializeCapabilities()
    {
        GLint max_texture_units = 0;
        glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &max_texture_units);
        capabilities_.max_sampled_textures_per_shader_stage =
            max_texture_units > 0 ? static_cast<uint32_t>(max_texture_units) : 0;

        capabilities_.bindless_textures = bindless_texture_table_ && bindless_texture_table_->IsReady();
        capabilities_.bindless_texture_table_capacity = capabilities_.bindless_textures
                                                             ? bindless_texture_table_->GetCapacity()
                                                             : 0;
    }

    void OpenglBackend::BeginFrame()
    {
        // OpenGL executes synchronously, so the previous frame's draws have
        // already produced the scene target; collect reads it before recording.
        CollectCompletedReadbacks();
        if (bindless_texture_table_)
        {
            bindless_texture_table_->BeginFrame(*texture_manager_, *sampler_manager_);
        }
        frame_active_ = true;
        active_render_target_ = {};
    }
    void OpenglBackend::EndFrame()
    {
        if (active_render_target_.IsValid())
        {
            EndRenderTarget();
        }
        // The editor's ImGui pass follows scene rendering on this context. It
        // supplies display-space colors, so scene sRGB write conversion must
        // never leak into the default framebuffer.
        glDisable(GL_FRAMEBUFFER_SRGB);
        if (bindless_texture_table_)
        {
            bindless_texture_table_->EndFrame();
        }
        frame_active_ = false;
    }

    void OpenglBackend::WaitIdle()
    {
        glFinish();
        CollectCompletedReadbacks();
        if (bindless_texture_table_)
        {
            bindless_texture_table_->WaitIdle();
        }
    }

    CommandRecorder *OpenglBackend::GetCommandRecorder()
    {
        return frame_active_ ? static_cast<CommandRecorder *>(this) : nullptr;
    }

    GraphicsContext OpenglBackend::GetGraphicsContext()
    {
        return CreateGraphicsContext();
    }

    void OpenglBackend::BeginRenderTarget(RenderTargetHandle target)
    {
        if (!frame_active_ || active_render_target_.IsValid())
        {
            return;
        }
        const uint32_t index = render_target_handles_.Get(target);
        if (index >= render_targets_.size() || index >= render_target_framebuffers_.size())
        {
            return;
        }
        const GLuint framebuffer = render_target_framebuffers_[index];
        if (framebuffer == 0)
        {
            return;
        }
        const RenderTargetResource &resource = render_targets_[index];
        glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
        glViewport(0, 0, static_cast<GLsizei>(resource.desc.width),
                   static_cast<GLsizei>(resource.desc.height));
        glClearColor(0.f, 0.f, 0.f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        active_render_target_ = target;
    }

    void OpenglBackend::EndRenderTarget()
    {
        if (!active_render_target_.IsValid())
        {
            return;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        // OpenglPipeline enables this for the sRGB scene color attachment. The
        // default framebuffer belongs to the subsequent UI renderer instead.
        glDisable(GL_FRAMEBUFFER_SRGB);
        active_render_target_ = {};
    }

    void OpenglBackend::BindPipeline(PipelineHandle pipeline)
    {
        OpenglPipeline *resource = pipeline_manager_->GetPipelineResource(pipeline);
        if (resource)
        {
            resource->Bind();
            if (bindless_texture_table_)
            {
                bindless_texture_table_->Bind();
            }
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
        // Cancel pending readbacks before any referenced attachment is destroyed.
        DrainPendingReadbacks("OpenGL backend shutdown");
        for (size_t index = 0; index < render_targets_.size(); ++index)
        {
            RenderTargetResource &target = render_targets_[index];
            if (index < render_target_framebuffers_.size() && render_target_framebuffers_[index] != 0)
            {
                glDeleteFramebuffers(1, &render_target_framebuffers_[index]);
            }
            if (target.color.IsValid())
            {
                DestroyTexture(target.color);
                DestroyTexture(target.depth);
                target = {};
            }
        }
        render_targets_.clear();
        render_target_framebuffers_.clear();
        pipeline_manager_->DestroyAll();
        resource_binding_sets_.clear();
        if (bindless_texture_table_)
        {
            bindless_texture_table_->Destroy();
            bindless_texture_table_.reset();
        }
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
        if (!ValidatePipelineDesc(pipeline_desc, GraphicsAPIType::GRAPHICS_API_OPENGL))
        {
            KP_LOG("OpenglBackendLog", LOG_LEVEL_ERROR,
                   "Rejected invalid OpenGL pipeline descriptor");
            return {};
        }
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
        if (bindless_texture_table_ && bindless_texture_table_->ReferencesTexture(handle))
        {
            KP_LOG("OpenglBackendLog", LOG_LEVEL_WARNING,
                   "Cannot destroy a texture referenced by the OpenGL bindless table");
            return false;
        }
        return texture_manager_->DestroyTexture(CreateGraphicsContext(), handle);
    }

    SamplerHandle OpenglBackend::CreateSampler(const SamplerSettings &settings)
    {
        return sampler_manager_->CreateSampler(CreateGraphicsContext(), settings);
    }

    bool OpenglBackend::DestroySampler(SamplerHandle handle)
    {
        if (bindless_texture_table_ && bindless_texture_table_->ReferencesSampler(handle))
        {
            KP_LOG("OpenglBackendLog", LOG_LEVEL_WARNING,
                   "Cannot destroy a sampler referenced by the OpenGL bindless table");
            return false;
        }
        return sampler_manager_->DestroySampler(CreateGraphicsContext(), handle);
    }

    RenderTargetHandle OpenglBackend::CreateRenderTarget(const RenderTargetDesc &desc)
    {
        if (desc.width == 0 || desc.height == 0)
        {
            return {};
        }
        const RenderTargetHandle handle = render_target_handles_.Create();
        if (handle.id == render_targets_.size())
        {
            render_targets_.emplace_back();
            render_target_framebuffers_.emplace_back(0);
        }

        TextureData target_data{};
        target_data.width = desc.width;
        target_data.height = desc.height;
        TextureSettings color_settings{};
        color_settings.format = desc.color_format;
        color_settings.usage = TextureUsage::TEXTURE_USAGE_COLOR_ATTACHMENT | TextureUsage::TEXTURE_USAGE_SAMPLE;
        color_settings.aspect = ImageAspect::IMAGE_ASPECT_COLOR;
        TextureSettings depth_settings{};
        depth_settings.format = desc.depth_format;
        depth_settings.usage = TextureUsage::TEXTURE_USAGE_DEPTHSTENCIL_ATTACHMENT;
        depth_settings.aspect = ImageAspect::IMAGE_ASPECT_DEPTH;

        RenderTargetResource target{};
        target.desc = desc;
        target.color = CreateTexture(target_data, color_settings);
        target.depth = CreateTexture(target_data, depth_settings);
        if (!target.color.IsValid() || !target.depth.IsValid())
        {
            if (target.color.IsValid()) DestroyTexture(target.color);
            if (target.depth.IsValid()) DestroyTexture(target.depth);
            render_target_handles_.Destroy(handle);
            return {};
        }
        Texture *color_texture = texture_manager_->GetTexture(target.color);
        Texture *depth_texture = texture_manager_->GetTexture(target.depth);
        if (!color_texture || !depth_texture)
        {
            DestroyTexture(target.color);
            DestroyTexture(target.depth);
            render_target_handles_.Destroy(handle);
            return {};
        }
        const OpenglTextureResource color_resource =
            ConvertToOpenglTextureResource(color_texture->GetTextueHandle());
        const OpenglTextureResource depth_resource =
            ConvertToOpenglTextureResource(depth_texture->GetTextueHandle());
        GLuint framebuffer = 0;
        glCreateFramebuffers(1, &framebuffer);
        glNamedFramebufferTexture(framebuffer, GL_COLOR_ATTACHMENT0, color_resource.image, 0);
        glNamedFramebufferTexture(framebuffer, GL_DEPTH_ATTACHMENT, depth_resource.image, 0);
        glNamedFramebufferDrawBuffer(framebuffer, GL_COLOR_ATTACHMENT0);
        if (glCheckNamedFramebufferStatus(framebuffer, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            glDeleteFramebuffers(1, &framebuffer);
            DestroyTexture(target.color);
            DestroyTexture(target.depth);
            render_target_handles_.Destroy(handle);
            return {};
        }
        render_targets_[handle.id] = target;
        render_target_framebuffers_[handle.id] = framebuffer;
        return handle;
    }

    bool OpenglBackend::DestroyRenderTarget(RenderTargetHandle handle)
    {
        if (render_target_readback_)
        {
            render_target_readback_->CancelTarget(
                handle, "Render target was destroyed before readback completed");
        }
        const uint32_t index = render_target_handles_.Get(handle);
        if (index >= render_targets_.size()) return false;
        RenderTargetResource &target = render_targets_[index];
        if (!target.color.IsValid() || !target.depth.IsValid()) return false;
        if (index < render_target_framebuffers_.size() && render_target_framebuffers_[index] != 0)
        {
            glDeleteFramebuffers(1, &render_target_framebuffers_[index]);
            render_target_framebuffers_[index] = 0;
        }
        DestroyTexture(target.color);
        DestroyTexture(target.depth);
        target = {};
        return render_target_handles_.Destroy(handle);
    }

    TextureHandle OpenglBackend::GetRenderTargetColor(RenderTargetHandle handle)
    {
        const uint32_t index = render_target_handles_.Get(handle);
        return index < render_targets_.size() ? render_targets_[index].color : TextureHandle{};
    }

    RenderTargetView OpenglBackend::GetRenderTargetView(RenderTargetHandle handle)
    {
        const uint32_t index = render_target_handles_.Get(handle);
        if (index >= render_targets_.size())
        {
            return {};
        }
        const RenderTargetResource &target = render_targets_[index];
        Texture *color_texture = texture_manager_->GetTexture(target.color);
        if (!color_texture)
        {
            return {};
        }
        const OpenglTextureResource resource =
            ConvertToOpenglTextureResource(color_texture->GetTextueHandle());
        return {target.desc.width, target.desc.height, resource.image, resource.image};
    }

    DescriptorSetHandle OpenglBackend::CreateResourceBindingSet(
        PipelineHandle pipeline, const ResourceBindingSetDesc &desc)
    {
        if (!pipeline_manager_->GetPipelineResource(pipeline))
        {
            return {};
        }
        const DescriptorSetHandle handle = resource_binding_set_handles_.Create();
        if (handle.id == resource_binding_sets_.size())
        {
            resource_binding_sets_.emplace_back();
        }

        auto set = std::make_unique<OpenglDescriptorSet>();
        bool valid = true;
        for (const ResourceBinding &binding : desc.bindings)
        {
            std::visit([&](const auto &value) {
                using Binding = std::decay_t<decltype(value)>;
                if constexpr (std::is_same_v<Binding, UniformBufferBinding>)
                {
                    if (!value.buffer.IsValid() || value.range == 0)
                    {
                        valid = false;
                        return;
                    }
                    set->SetUniformBuffer(value.binding, value.buffer.id,
                                          value.offset, value.range);
                }
                else
                {
                    Texture *texture = texture_manager_->GetTexture(value.texture);
                    Sampler *sampler = sampler_manager_->GetSampler(value.sampler);
                    if (!texture || !sampler)
                    {
                        valid = false;
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
        if (!valid)
        {
            resource_binding_set_handles_.Destroy(handle);
            return {};
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

    BindlessTextureHandle OpenglBackend::AcquireBindlessTexture(TextureHandle texture,
                                                                 SamplerHandle sampler)
    {
        return bindless_texture_table_
                   ? bindless_texture_table_->Acquire(texture, sampler, *texture_manager_, *sampler_manager_)
                   : BindlessTextureHandle{};
    }

    bool OpenglBackend::ReleaseBindlessTexture(BindlessTextureHandle handle)
    {
        if (!bindless_texture_table_)
        {
            return false;
        }
        const BindlessSubmissionSerial retire_after = frame_active_
                                                          ? bindless_texture_table_->GetPendingSubmissionSerial()
                                                          : bindless_texture_table_->GetLastSubmittedSerial();
        return bindless_texture_table_->Release(handle, retire_after);
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

    GraphicsContext OpenglBackend::CreateGraphicsContext()
    {
        GraphicsContext context;
        context.native = &context_;
        context.type = GraphicsAPIType::GRAPHICS_API_OPENGL;
        return context;
    }

    bool OpenglBackend::EnqueueRenderTargetReadback(RenderTargetReadbackRequest request,
                                                     RenderTargetReadbackCallback on_completed)
    {
        return render_target_readback_ &&
               render_target_readback_->Enqueue(request, std::move(on_completed));
    }

    void OpenglBackend::CollectCompletedReadbacks()
    {
        if (render_target_readback_)
        {
            render_target_readback_->CollectCompleted();
        }
    }

    void OpenglBackend::DrainPendingReadbacks(std::string diagnostic)
    {
        if (render_target_readback_)
        {
            render_target_readback_->Drain(std::move(diagnostic));
        }
    }

    OpenglRenderTargetReadbackSource OpenglBackend::GetRenderTargetReadbackSource(
        RenderTargetHandle handle) const
    {
        OpenglRenderTargetReadbackSource source{};
        const uint32_t index = render_target_handles_.Get(handle);
        if (index >= render_targets_.size())
        {
            return source;
        }
        const RenderTargetResource &target = render_targets_[index];
        if (!target.color.IsValid())
        {
            return source;
        }
        Texture *const color_texture = texture_manager_->GetTexture(target.color);
        if (!color_texture)
        {
            return source;
        }
        const OpenglTextureResource resource =
            ConvertToOpenglTextureResource(color_texture->GetTextueHandle());
        source.image = resource.image;
        source.width = target.desc.width;
        source.height = target.desc.height;
        return source;
    }

}
