#ifndef KPENGINE_RUNTIME_RENDER_FRAME_CONTEXT_H
#define KPENGINE_RUNTIME_RENDER_FRAME_CONTEXT_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#include "graphics/backend/common/render_backend.h"

namespace kpengine::render
{
    // Data shared by all scene work submitted for one render frame.
    struct FrameGlobals
    {
        uint64_t frame_number = 0;
        float elapsed_seconds = 0.0f;
        float delta_seconds = 0.0f;
    };

    struct UniformAllocation
    {
        graphics::BufferHandle buffer;
        size_t offset = 0;
        size_t range = 0;
        void *mapped = nullptr;

        bool IsValid() const { return buffer.IsValid() && mapped != nullptr && range != 0; }
    };

    // RenderSystem owns one logical context per backend frame slot. The backend
    // remains responsible for fences and deciding when that slot is safe to
    // recycle; this type will own only the render-layer transient allocations.
    class FrameContext
    {
    public:
        uint32_t GetFrameIndex() const { return frame_index_; }
        const FrameGlobals &GetGlobals() const { return globals_; }
        bool IsActive() const { return active_; }
        size_t GetUniformCapacity() const { return uniform_capacity_; }
        size_t GetUniformUsed() const { return uniform_cursor_; }
        graphics::Extent2D GetRenderExtent() const { return render_extent_; }

        UniformAllocation AllocateUniform(size_t size);
        graphics::DescriptorSetHandle AllocateResourceBindingSet(
            graphics::PipelineHandle pipeline,
            const graphics::ResourceBindingSetDesc &desc);

        template <typename T>
        UniformAllocation AllocateUniform(const T &value)
        {
            UniformAllocation allocation = AllocateUniform(sizeof(T));
            if (allocation.IsValid())
            {
                std::memcpy(allocation.mapped, &value, sizeof(T));
            }
            return allocation;
        }

        // RenderSystem is the normal owner. The explicit lifecycle also keeps the
        // standalone RHI example able to exercise the same render-layer path.
        void Initialize(graphics::RenderBackend &backend, size_t uniform_capacity);
        void Begin(uint32_t frame_index, const FrameGlobals &globals,
                   graphics::Extent2D render_extent);
        void End();
        void Cleanup();

    private:
        void ReleaseTransientBindings();

        graphics::RenderBackend *backend_ = nullptr;
        graphics::BufferHandle uniform_buffer_;
        void *uniform_mapped_ = nullptr;
        size_t uniform_capacity_ = 0;
        size_t uniform_alignment_ = 1;
        size_t uniform_cursor_ = 0;
        uint32_t frame_index_ = 0;
        FrameGlobals globals_;
        graphics::Extent2D render_extent_;
        std::vector<graphics::DescriptorSetHandle> transient_binding_sets_;
        bool active_ = false;
    };
}

#endif
