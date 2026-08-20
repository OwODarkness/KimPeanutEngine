#ifndef KPENGINE_RUNTIME_GRAPHICS_OPENGL_BACKEND_H
#define KPENGINE_RUNTIME_GRAPHICS_OPENGL_BACKEND_H

#include <memory>
#include <unordered_map>
#include <vector>
#include <glad/glad.h>

#include "common/render_backend.h"
#include "math/math_header.h"
#include "opengl_context.h"

namespace kpengine::graphics
{   
     struct UniformBuffer
    {
        alignas(16) Matrix4f model;
        alignas(16) Matrix4f view;
        alignas(16) Matrix4f proj;
    };


    class OpenglBackend : public RenderBackend, public CommandRecorder
    {
    public:
        OpenglBackend();
        ~OpenglBackend();
    public:
        virtual void Initialize(WindowHandle native_window) override;
        PipelineHandle CreatePipelineResource(const PipelineDesc &pipeline_desc) override;
        bool DestroyPipelineResource(PipelineHandle handle) override;
        MeshHandle CreateMesh(const data::MeshData &data) override;
        bool DestroyMesh(MeshHandle handle) override;
        TextureHandle CreateTexture(const data::TextureData &data,
                                    const TextureSettings &settings) override;
        bool DestroyTexture(TextureHandle handle) override;
        SamplerHandle CreateSampler(const SamplerSettings &settings) override;
        bool DestroySampler(SamplerHandle handle) override;
        DescriptorSetHandle CreateResourceBindingSet(
            PipelineHandle pipeline, const ResourceBindingSetDesc &desc) override;
        bool DestroyResourceBindingSet(DescriptorSetHandle handle) override;
        void BindResourceBindingSet(PipelineHandle pipeline,
                                    DescriptorSetHandle handle) override;
        virtual void BeginFrame() override;
        CommandRecorder *GetCommandRecorder() override;
        void BindPipeline(PipelineHandle pipeline) override;
        void BindMesh(MeshHandle mesh) override;
        void BindResourceBindings(PipelineHandle pipeline,
                                   DescriptorSetHandle bindings) override;
        void SetViewport(const Viewport &viewport) override;
        void SetScissor(const Scissor &scissor) override;
        void DrawIndexed(uint32_t index_count, uint32_t instance_count,
                         uint32_t first_index, int32_t vertex_offset,
                         uint32_t first_instance) override;
        virtual void EndFrame() override;
        BufferHandle CreateUniformBuffer(uint32_t size) override;
        void *MapUniformBuffer(BufferHandle handle, size_t size) override;
        uint32_t GetCurrentFrameIndex() const override { return 0; }
        uint32_t GetFramesInFlight() const override { return 1; }
        size_t GetUniformBufferAlignment() const override;
        Extent2D GetRenderExtent() const override;
        virtual void Cleanup() override;
    public:
        BufferHandle CreateVertexBuffer(const void* data, size_t size) override;
        BufferHandle CreateIndexBuffer(const void* data, size_t size) override;
        bool DestroyBufferResource(BufferHandle handle) override;

    private:
        void CreateMeshes();
        void CreateUniformBuffers();
        void CreateTextures();
        void UpdateUniformBuffers();
        GraphicsContext CreateGraphicsContext();
        void CreateDescriptorSets();
    private:
        std::unique_ptr<class MeshManager> mesh_manager_;
        std::unique_ptr<class TextureManager> texture_manager_;
        std::unique_ptr<class SamplerManager> sampler_manager_;
        std::unique_ptr<class OpenglPipelineManager> pipeline_manager_;


        OpenglContext context_;
        std::vector<std::unique_ptr<class OpenglDescriptorSet>> resource_binding_sets_;
        HandleSystem<DescriptorSetHandle> resource_binding_set_handles_;
        std::unique_ptr<class OpenglDescriptorSet> descriptor_set;
        TextureHandle texture_handle;
        SamplerHandle sampler_handle;
        MeshHandle mesh_handle;
        std::vector<GLuint> ubos_;
        struct MappedUniformBuffer
        {
            GLuint native = 0;
            std::vector<uint8_t> data;
        };
        std::unordered_map<uint32_t, MappedUniformBuffer> mapped_uniform_buffers_;
        MeshHandle recorded_mesh_;
        uint32_t recorded_index_count_ = 0;
        PipelineHandle recorded_pipeline_;
        bool frame_active_ = false;

    };
}

#endif
