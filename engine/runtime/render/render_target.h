#ifndef KPENGINE_RUNTIME_RENDER_RENDER_TARGET_H
#define KPENGINE_RUNTIME_RENDER_RENDER_TARGET_H

#include <cstdint>
#include <memory>

#include "graphics/backend/common/render_target.h"

namespace kpengine::graphics
{
    class RenderBackend;
    class CommandRecorder;
}

namespace kpengine::render
{
    class RenderSystem;

    // Render-module output target. Its RHI attachments remain private so editor,
    // gameplay, and other high-level modules never need Graphics handles.
    class RenderTarget
    {
    public:
        RenderTarget();
        ~RenderTarget();
        RenderTarget(const RenderTarget &) = delete;
        RenderTarget &operator=(const RenderTarget &) = delete;

        bool IsValid() const;
        uint32_t GetWidth() const;
        uint32_t GetHeight() const;
        graphics::RenderTargetHandle GetHandle() const;
        graphics::RenderTargetView GetView() const;

    private:
        friend class RenderSystem;

        void Initialize(graphics::RenderBackend &backend, uint32_t width, uint32_t height);
        void Cleanup();
        bool BeginRecording(graphics::CommandRecorder &recorder) const;
        void EndRecording(graphics::CommandRecorder &recorder) const;

    private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
    };
}

#endif
