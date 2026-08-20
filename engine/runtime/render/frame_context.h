#ifndef KPENGINE_RUNTIME_RENDER_FRAME_CONTEXT_H
#define KPENGINE_RUNTIME_RENDER_FRAME_CONTEXT_H

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "graphics/backend/common/api.h"

namespace kpengine::graphics
{
    class RenderBackend;
}

namespace kpengine::render
{
    // Data shared by all scene work submitted for one render frame. It is
    // deliberately CPU-only at this seam: Phase 3.4 will add frame-local GPU
    // allocations without letting scenes own their backing buffers.
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

        UniformAllocation AllocateUniform(size_t size);

        template <typename T>
        UniformAllocation AllocateUniform(const T &value)
        {
            static_assert(std::is_trivially_copyable_v<T>,
                          "Frame uniform data must be trivially copyable");
            UniformAllocation allocation = AllocateUniform(sizeof(T));
            if (allocation.IsValid())
            {
                std::memcpy(allocation.mapped, &value, sizeof(T));
            }
            return allocation;
        }

    private:
        friend class RenderSystem;

        void Initialize(graphics::RenderBackend &backend, size_t uniform_capacity);
        void Begin(uint32_t frame_index, const FrameGlobals &globals);
        void End();
        void Cleanup();

    private:
        graphics::RenderBackend *backend_ = nullptr;
        graphics::BufferHandle uniform_buffer_;
        void *uniform_mapped_ = nullptr;
        size_t uniform_capacity_ = 0;
        size_t uniform_alignment_ = 1;
        size_t uniform_cursor_ = 0;
        uint32_t frame_index_ = 0;
        FrameGlobals globals_;
        bool active_ = false;
    };
}

#endif
