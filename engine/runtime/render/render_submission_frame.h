#ifndef KPENGINE_RUNTIME_RENDER_SUBMISSION_FRAME_H
#define KPENGINE_RUNTIME_RENDER_SUBMISSION_FRAME_H

#include <cstddef>

#include "graphics/backend/common/api.h"
#include "graphics/backend/common/resource_binding.h"

namespace kpengine::render
{
    // One frame-lifetime allocation from the frame's uniform ring. Valid only
    // until the owning frame slot is recycled.
    struct UniformAllocation
    {
        graphics::BufferHandle buffer;
        std::size_t offset = 0;
        std::size_t range = 0;
        void *mapped = nullptr;

        bool IsValid() const { return buffer.IsValid() && mapped != nullptr && range != 0; }
    };

    // The frame-lifetime services a generic submission needs. `FrameContext` is
    // the production implementation; the seam exists so submission execution can
    // be validated without a graphics backend.
    class RenderSubmissionFrame
    {
    public:
        virtual ~RenderSubmissionFrame() = default;
        virtual bool IsActive() const = 0;
        virtual UniformAllocation AllocateUniform(std::size_t size) = 0;
        virtual graphics::DescriptorSetHandle AllocateResourceBindingSet(
            graphics::PipelineHandle pipeline,
            const graphics::ResourceBindingSetDesc &desc) = 0;
        virtual bool WriteFrameBuffer(graphics::BufferHandle buffer,
                                      std::size_t offset, const void *data,
                                      std::size_t size) = 0;
    };
}

#endif
