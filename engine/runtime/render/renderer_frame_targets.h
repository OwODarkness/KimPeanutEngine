#ifndef KPENGINE_RUNTIME_RENDER_RENDERER_FRAME_TARGETS_H
#define KPENGINE_RUNTIME_RENDER_RENDERER_FRAME_TARGETS_H

#include <cstdint>
#include <memory>

#include "render_target.h"

namespace kpengine::graphics
{
    class RenderBackend;
}

namespace kpengine::render
{
    // Named, Render-private logical target set for one extent/format policy.
    // Passes request a target by name; this is never a graph allocator.
    enum class RenderTargetName : uint8_t
    {
        SceneColor,
        GBuffer,
        DirectionalShadow,
        SpotShadow,
        PointShadow,
        SceneHdr,
        CaptureOutput,
        Count,
    };
    inline constexpr uint32_t kRenderTargetNameCount =
        static_cast<uint32_t>(RenderTargetName::Count);

    class RendererFrameTargets
    {
    public:
        RendererFrameTargets();
        ~RendererFrameTargets();
        RendererFrameTargets(const RendererFrameTargets &) = delete;
        RendererFrameTargets &operator=(const RendererFrameTargets &) = delete;

        void Initialize(graphics::RenderBackend &backend, uint32_t width, uint32_t height);
        // Rebuilds every target only when the extent changed, so a stable size
        // keeps GPU generations intact across frames. Retires via WaitIdle at a
        // render-system ownership boundary before recreating.
        void RebuildForExtent(graphics::RenderBackend &backend, uint32_t width, uint32_t height);
        void Cleanup();

        RenderTarget *GetTarget(RenderTargetName name);
        const RenderTarget *GetTarget(RenderTargetName name) const;

    private:
        graphics::RenderTargetDesc BuildDesc(RenderTargetName name, uint32_t width,
                                             uint32_t height) const;

        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

#endif
