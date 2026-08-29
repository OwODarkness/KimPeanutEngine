#ifndef KPENGINE_RUNTIME_GRAPHICS_RENDER_BACKEND_H
#define KPENGINE_RUNTIME_GRAPHICS_RENDER_BACKEND_H

#include <memory>
#include "base/base.h"
#include "delegate/event_dispatcher.h"
#include "math/math_header.h"
#include "api.h"
#include "bindless_texture.h"
#include "command_recorder.h"
#include "graphics_capabilities.h"
#include "mesh.h"
#include "pipeline_types.h"
#include "resource_binding.h"
#include "render_target.h"
#include "render_target_readback.h"
#include "sampler.h"
#include "texture.h"

namespace kpengine::graphics
{

    struct CameraData
    {
        Matrix4f view;
        Matrix4f proj;
    };

    struct PerPassData
    {
        CameraData camera_data;
    };

    struct PerObjectData
    {
        Matrix4f model;
    };

    struct Extent2D
    {
        uint32_t width = 0;
        uint32_t height = 0;
    };

    class RenderBackend
    {
    public:
        static std::unique_ptr<RenderBackend> CreateGraphicsBackEnd(GraphicsAPIType backend_type);

    public:
        virtual void Initialize(WindowHandle native_window) = 0;
        virtual PipelineHandle CreatePipelineResource(const PipelineDesc &pipeline_desc) = 0;
        virtual bool DestroyPipelineResource(PipelineHandle handle) = 0;
        virtual MeshHandle CreateMesh(const data::MeshData &data) = 0;
        virtual bool DestroyMesh(MeshHandle handle) = 0;
        virtual TextureHandle CreateTexture(const data::TextureData &data,
                                            const TextureSettings &settings) = 0;
        virtual bool DestroyTexture(TextureHandle handle) = 0;
        virtual SamplerHandle CreateSampler(const SamplerSettings &settings) = 0;
        virtual bool DestroySampler(SamplerHandle handle) = 0;
        virtual RenderTargetHandle CreateRenderTarget(const RenderTargetDesc &desc) = 0;
        virtual bool DestroyRenderTarget(RenderTargetHandle handle) = 0;
        virtual TextureHandle GetRenderTargetColor(RenderTargetHandle handle) = 0;
        // Returns an invalid handle when the target has no depth attachment or
        // the color index is out of range.
        virtual TextureHandle GetRenderTargetColorAttachment(RenderTargetHandle handle,
                                                             uint32_t index) = 0;
        virtual TextureHandle GetRenderTargetDepthAttachment(RenderTargetHandle handle) = 0;
        // Returns depth only when the target was created with shader-readable
        // depth. Callers intending to sample a target must use this accessor.
        virtual TextureHandle GetRenderTargetSampledDepthAttachment(
            RenderTargetHandle handle) = 0;
        virtual RenderTargetView GetRenderTargetView(RenderTargetHandle handle) = 0;
        virtual IRenderTargetReadback *GetRenderTargetReadback() { return nullptr; }
        virtual DescriptorSetHandle CreateResourceBindingSet(
            PipelineHandle pipeline, const ResourceBindingSetDesc &desc) = 0;
        virtual bool DestroyResourceBindingSet(DescriptorSetHandle handle) = 0;
        // Optional sampled-texture table. The default records no slot so a
        // backend that has not enabled the full common contract cannot expose
        // a partial native bindless feature.
        virtual BindlessTextureHandle AcquireBindlessTexture(TextureHandle texture,
                                                              SamplerHandle sampler)
        {
            (void)texture;
            (void)sampler;
            return {};
        }
        virtual bool ReleaseBindlessTexture(BindlessTextureHandle handle)
        {
            (void)handle;
            return false;
        }
        virtual void BindResourceBindingSet(PipelineHandle pipeline,
                                            DescriptorSetHandle handle) = 0;
        virtual void BeginFrame() = 0;
        virtual CommandRecorder *GetCommandRecorder() = 0;
        virtual void EndFrame() = 0;
        virtual GraphicsContext GetGraphicsContext() = 0;
        const GraphicsCapabilities &GetCapabilities() const { return capabilities_; }
        virtual BufferHandle CreateUniformBuffer(uint32_t size) = 0;
        virtual void *MapUniformBuffer(BufferHandle handle, size_t size) = 0;
        virtual uint32_t GetCurrentFrameIndex() const = 0;
        virtual uint32_t GetFramesInFlight() const = 0;
        virtual size_t GetUniformBufferAlignment() const = 0;
        virtual Extent2D GetRenderExtent() const = 0;
        // Call only at a render-system ownership boundary before replacing GPU
        // resources shared by previously submitted frames.
        virtual void WaitIdle() = 0;
        virtual void Cleanup() = 0;
        void BindWindowResize(EventDispatcher<ResizeEvent> &dispatcher);

    public:
        // bytes
        virtual BufferHandle CreateVertexBuffer(const void *data, size_t size) = 0;
        virtual BufferHandle CreateIndexBuffer(const void *data, size_t size) = 0;
        virtual bool DestroyBufferResource(BufferHandle) = 0;

    protected:
        virtual void FramebufferResizeCallback(const ResizeEvent &event);

    public:
        RenderBackend() = default;
        virtual ~RenderBackend() = default;
        RenderBackend(const RenderBackend &) = delete;
        RenderBackend &operator=(const RenderBackend &) = delete;

    protected:
        int width_ = 0;
        int height_ = 0;
        GraphicsCapabilities capabilities_{};
    };
}

#endif
