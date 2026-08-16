#ifndef KPENGINE_RUNTIME_GRAPHICS_RENDER_BACKEND_H
#define KPENGINE_RUNTIME_GRAPHICS_RENDER_BACKEND_H

#include <memory>
#include "base/base.h"
#include "delegate/event_dispatcher.h"
#include "math/math_header.h"
#include "api.h"
#include "pipeline_types.h"

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

    class RenderBackend
    {
    public:
        static std::unique_ptr<RenderBackend> CreateGraphicsBackEnd(GraphicsAPIType backend_type);

    public:
        // The native window handle (WindowHandle = void*) is the WSI surface the
        // backend creates its swapchain/present surface on. Explicit param, not a
        // mutable public member — the RHI never needs to reach back for it.
        virtual void Initialize(const PipelineDesc &pipeline_desc, WindowHandle native_window) = 0;
        virtual void BeginFrame() = 0;
        virtual void EndFrame() = 0;
        virtual void Cleanup() = 0;
        virtual void Present() = 0;
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
    };
}

#endif