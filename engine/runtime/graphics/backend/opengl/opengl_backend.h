#ifndef KPENGINE_RUNTIME_GRAPHICS_OPENGL_BACKEND_H
#define KPENGINE_RUNTIME_GRAPHICS_OPENGL_BACKEND_H

#include "common/render_backend.h"

namespace kpengine::graphics
{
    class OpenglBackend : public RenderBackend
    {
    public:
        virtual void Initialize() override;
        virtual void BeginFrame() override;
        virtual void EndFrame() override;
        virtual void Present() override;
        virtual void Cleanup() override;
    protected:
        BufferHandle CreateVertexBuffer(const void* data, size_t size) override;
        BufferHandle CreateIndexBuffer(const void* data, size_t size) override;
        void DestroyBuffer(BufferHandle handle) override;
    };
}

#endif