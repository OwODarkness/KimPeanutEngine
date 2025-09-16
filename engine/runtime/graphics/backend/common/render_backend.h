#ifndef KPENGINE_RUNTIME_GRAPHICS_RENDER_BACKEND_H
#define KPENGINE_RUNTIME_GRAPHICS_RENDER_BACKEND_H

#include <memory>
#include "base/base.h"
#include "delegate/event_dispatcher.h"
#include "shader_manager.h"
#include "api.h"
struct GLFWwindow;
namespace kpengine::graphics{

    
class RenderBackend{
public:
    static std::unique_ptr<RenderBackend> CreateGraphicsBackEnd(GraphicsAPIType backend_type);
public:
    virtual void Initialize() = 0;
    virtual void BeginFrame() = 0;
    virtual void EndFrame() = 0;
    virtual void Cleanup() = 0;
    virtual void Present() = 0;
    void BindWindowResize(EventDispatcher<ResizeEvent> &dispatcher);

protected:
    virtual BufferHandle CreateVertexBuffer(const void* data, size_t size) = 0;
    virtual BufferHandle CreateIndexBuffer(const void* data, size_t size) = 0;
    virtual bool DestroyBufferResource(BufferHandle) = 0;
    virtual void FramebufferResizeCallback(const ResizeEvent& event) ;
public:
    RenderBackend() = default;
    virtual ~RenderBackend() = default;
    RenderBackend(const RenderBackend & ) = delete;
    RenderBackend& operator=(const RenderBackend &) = delete;
public:
    GLFWwindow* window_;
protected:
    ShaderManager shader_manager_;
    int width_ = 0;
    int height_ = 0;
};
}

#endif