#ifndef KPENGINE_RUNTIME_GRAPHICS_OPENGL_RENDER_TARGET_READBACK_H
#define KPENGINE_RUNTIME_GRAPHICS_OPENGL_RENDER_TARGET_READBACK_H

#include <functional>
#include <mutex>
#include <string>
#include <vector>

#include <glad/glad.h>

#include "common/render_target_readback.h"

namespace kpengine::graphics
{
    // Native source the backend resolves for a readback request. The backend owns
    // render-target bookkeeping; this value is all the readback needs to collect.
    struct OpenglRenderTargetReadbackSource
    {
        GLuint image = 0;
        uint32_t width = 0;
        uint32_t height = 0;

        bool IsValid() const { return image != 0 && width != 0 && height != 0; }
    };

    // OpenGL-private readback owner. OpenGL executes commands synchronously, so
    // there is no pending/staging lifecycle: enqueue validates the request and
    // collect performs one direct texture read. Ownership mirrors the Vulkan path.
    class OpenglRenderTargetReadback final : public IRenderTargetReadback
    {
    public:
        explicit OpenglRenderTargetReadback(
            std::function<OpenglRenderTargetReadbackSource(RenderTargetHandle)> resolve_source);

        bool EnqueueRenderTargetReadback(RenderTargetReadbackRequest request,
                                         RenderTargetReadbackCallback on_completed) override;
        void CollectCompletedReadbacks() override;
        void CancelTarget(RenderTargetHandle target, std::string diagnostic);
        void DrainPendingReadbacks(std::string diagnostic) override;

    private:
        struct PendingReadback
        {
            RenderTargetReadbackRequest request;
            RenderTargetReadbackCallback on_completed;
        };

        void CompleteCancelled(std::vector<PendingReadback> pending, const std::string &diagnostic);

        std::function<OpenglRenderTargetReadbackSource(RenderTargetHandle)> resolve_source_;
        std::mutex mutex_;
        std::vector<PendingReadback> queued_;
    };
}

#endif
