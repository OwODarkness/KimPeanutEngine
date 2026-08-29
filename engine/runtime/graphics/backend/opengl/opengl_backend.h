#ifndef KPENGINE_RUNTIME_GRAPHICS_OPENGL_BACKEND_H
#define KPENGINE_RUNTIME_GRAPHICS_OPENGL_BACKEND_H

#include <memory>
#include <unordered_map>
#include <vector>
#include <glad/glad.h>

#include "common/render_backend.h"
#include "opengl_command_recorder.h"
#include "opengl_context.h"

namespace kpengine::graphics
{
    struct OpenglRenderTargetReadbackSource;

    class OpenglBackend : public RenderBackend
    {
    public:
        OpenglBackend();
        ~OpenglBackend();
    public:
        virtual void Initialize(WindowHandle native_window) override;
        IRenderTargetReadback *GetRenderTargetReadback() override;
        PipelineHandle CreatePipelineResource(const PipelineDesc &pipeline_desc) override;
        bool DestroyPipelineResource(PipelineHandle handle) override;
        MeshHandle CreateMesh(const data::MeshData &data) override;
        bool DestroyMesh(MeshHandle handle) override;
        TextureHandle CreateTexture(const data::TextureData &data,
                                    const TextureSettings &settings) override;
        bool DestroyTexture(TextureHandle handle) override;
        SamplerHandle CreateSampler(const SamplerSettings &settings) override;
        bool DestroySampler(SamplerHandle handle) override;
        RenderTargetHandle CreateRenderTarget(const RenderTargetDesc &desc) override;
        bool DestroyRenderTarget(RenderTargetHandle handle) override;
        TextureHandle GetRenderTargetColor(RenderTargetHandle handle) override;
        TextureHandle GetRenderTargetColorAttachment(RenderTargetHandle handle,
                                                     uint32_t index) override;
        TextureHandle GetRenderTargetDepthAttachment(RenderTargetHandle handle) override;
        TextureHandle GetRenderTargetSampledDepthAttachment(RenderTargetHandle handle) override;
        RenderTargetView GetRenderTargetView(RenderTargetHandle handle) override;
        DescriptorSetHandle CreateResourceBindingSet(
            PipelineHandle pipeline, const ResourceBindingSetDesc &desc) override;
        bool DestroyResourceBindingSet(DescriptorSetHandle handle) override;
        BindlessTextureHandle AcquireBindlessTexture(TextureHandle texture,
                                                     SamplerHandle sampler) override;
        bool ReleaseBindlessTexture(BindlessTextureHandle handle) override;
        void BindResourceBindingSet(PipelineHandle pipeline,
                                    DescriptorSetHandle handle) override;
        virtual void BeginFrame() override;
        CommandRecorder *GetCommandRecorder() override;
        virtual void EndFrame() override;
        GraphicsContext GetGraphicsContext() override;
        BufferHandle CreateUniformBuffer(uint32_t size) override;
        void *MapUniformBuffer(BufferHandle handle, size_t size) override;
        uint32_t GetCurrentFrameIndex() const override { return 0; }
        uint32_t GetFramesInFlight() const override { return 1; }
        size_t GetUniformBufferAlignment() const override;
        Extent2D GetRenderExtent() const override;
        void WaitIdle() override;
        virtual void Cleanup() override;
    public:
        BufferHandle CreateVertexBuffer(const void* data, size_t size) override;
        BufferHandle CreateIndexBuffer(const void* data, size_t size) override;
        bool DestroyBufferResource(BufferHandle handle) override;

    private:
        void InitializeCapabilities();
        GraphicsContext CreateGraphicsContext();
        OpenglRenderTargetReadbackSource GetRenderTargetReadbackSource(
            RenderTargetHandle handle) const;
    private:
        std::unique_ptr<class MeshManager> mesh_manager_;
        std::unique_ptr<class TextureManager> texture_manager_;
        std::unique_ptr<class SamplerManager> sampler_manager_;
        std::unique_ptr<class OpenglPipelineManager> pipeline_manager_;
        std::unique_ptr<class OpenglBindlessTextureTable> bindless_texture_table_;
        std::unique_ptr<class OpenglRenderTargetReadback> render_target_readback_;

        std::vector<RenderTargetResource> render_targets_;
        std::vector<GLuint> render_target_framebuffers_;
        HandleSystem<RenderTargetHandle> render_target_handles_;


        OpenglContext context_;
        std::vector<std::unique_ptr<class OpenglDescriptorSet>> resource_binding_sets_;
        HandleSystem<DescriptorSetHandle> resource_binding_set_handles_;
        std::unordered_map<uint32_t, OpenglMappedUniformBuffer> mapped_uniform_buffers_;
        std::unique_ptr<OpenglCommandRecorder> command_recorder_;
        bool frame_active_ = false;

    };
}

#endif
