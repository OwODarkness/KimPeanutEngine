#ifndef KPENGINE_RUNTIME_RENDER_RENDER_EXTENSION_H
#define KPENGINE_RUNTIME_RENDER_RENDER_EXTENSION_H

#include <cstdint>
#include <string>

#include "graphics/backend/common/command_recorder.h"
#include "graphics/backend/common/render_target.h"

namespace kpengine::graphics
{
    class RenderBackend;
}

namespace kpengine::render
{
    class FrameContext;

    // Optional renderer-owned work recorded after the main scene and before
    // editor presentation. The extension owns its GPU resources; RenderSystem
    // owns only ordering and frame lifetime.
    class IRenderExtension
    {
    public:
        virtual ~IRenderExtension() = default;

        virtual const char *GetName() const noexcept = 0;
        virtual bool Initialize(graphics::RenderBackend &backend,
                                uint32_t width, uint32_t height,
                                std::string &diagnostic) = 0;
        virtual bool Record(FrameContext &frame_context,
                            graphics::CommandRecorder &recorder,
                            float delta_time,
                            std::string &diagnostic) = 0;
        virtual graphics::RenderTargetHandle GetOutputTarget() const = 0;
        virtual graphics::RenderTargetView GetOutputView() const = 0;
        virtual void Cleanup() noexcept = 0;
    };
}

#endif
