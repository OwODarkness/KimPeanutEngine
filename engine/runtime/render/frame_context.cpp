#include "frame_context.h"

#include <stdexcept>

#include "graphics/backend/common/render_backend.h"

namespace kpengine::render
{
    namespace
    {
        size_t AlignUp(size_t value, size_t alignment)
        {
            return (value + alignment - 1) / alignment * alignment;
        }
    }

    void FrameContext::Initialize(graphics::RenderBackend &backend, size_t uniform_capacity)
    {
        backend_ = &backend;
        uniform_alignment_ = backend.GetUniformBufferAlignment();
        if (uniform_alignment_ == 0)
        {
            uniform_alignment_ = 1;
        }
        uniform_capacity_ = AlignUp(uniform_capacity, uniform_alignment_);
        uniform_buffer_ = backend.CreateUniformBuffer(static_cast<uint32_t>(uniform_capacity_));
        uniform_mapped_ = backend.MapUniformBuffer(uniform_buffer_, uniform_capacity_);
        if (!uniform_buffer_.IsValid() || !uniform_mapped_)
        {
            throw std::runtime_error("Failed to initialize frame uniform allocator");
        }
    }

    void FrameContext::Begin(uint32_t frame_index, const FrameGlobals &globals,
                             graphics::Extent2D render_extent)
    {
        if (!backend_ || !uniform_mapped_)
        {
            throw std::runtime_error("FrameContext is not initialized");
        }
        frame_index_ = frame_index;
        globals_ = globals;
        render_extent_ = render_extent;
        ReleaseTransientBindings();
        uniform_cursor_ = 0;
        active_ = true;
    }

    void FrameContext::End()
    {
        active_ = false;
    }

    UniformAllocation FrameContext::AllocateUniform(size_t size)
    {
        if (!active_ || size == 0)
        {
            return {};
        }
        const size_t offset = AlignUp(uniform_cursor_, uniform_alignment_);
        if (offset > uniform_capacity_ || size > uniform_capacity_ - offset)
        {
            return {};
        }
        uniform_cursor_ = offset + size;
        return {uniform_buffer_, offset, size,
                static_cast<uint8_t *>(uniform_mapped_) + offset};
    }

    graphics::DescriptorSetHandle FrameContext::AllocateResourceBindingSet(
        graphics::PipelineHandle pipeline, const graphics::ResourceBindingSetDesc &desc)
    {
        if (!active_ || !backend_ || !pipeline.IsValid())
        {
            return {};
        }
        const graphics::DescriptorSetHandle handle =
            backend_->CreateResourceBindingSet(pipeline, desc);
        if (handle.IsValid())
        {
            transient_binding_sets_.push_back(handle);
        }
        return handle;
    }

    void FrameContext::Cleanup()
    {
        ReleaseTransientBindings();
        if (backend_ && uniform_buffer_.IsValid())
        {
            backend_->DestroyBufferResource(uniform_buffer_);
        }
        backend_ = nullptr;
        uniform_buffer_ = {};
        uniform_mapped_ = nullptr;
        uniform_capacity_ = 0;
        uniform_alignment_ = 1;
        uniform_cursor_ = 0;
        render_extent_ = {};
        active_ = false;
    }

    void FrameContext::ReleaseTransientBindings()
    {
        if (backend_)
        {
            for (graphics::DescriptorSetHandle handle : transient_binding_sets_)
            {
                backend_->DestroyResourceBindingSet(handle);
            }
        }
        transient_binding_sets_.clear();
    }
}
