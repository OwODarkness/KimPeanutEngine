#ifndef KPENGINE_RUNTIME_GRAPHICS_BINDLESS_TEXTURE_H
#define KPENGINE_RUNTIME_GRAPHICS_BINDLESS_TEXTURE_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "api.h"

namespace kpengine::graphics
{
    // Stable, backend-neutral shader ABI for the optional sampled-texture
    // table. Backend shader preprocessing translates this logical convention
    // to its native source form; it must not expose native texture handles.
    struct BindlessTextureTableLayout
    {
        static constexpr uint32_t shader_abi_version = 1;
        static constexpr uint32_t descriptor_set = 1;
        static constexpr uint32_t descriptor_binding = 0;
        static constexpr uint32_t max_capacity = 4096;
    };

    // A generational slot in the Graphics-owned sampled-texture table. An
    // invalid handle means the backend cannot allocate the optional path and
    // callers must retain or select their ordinary bound-resource fallback.
    struct BindlessTextureTag {};
    using BindlessTextureHandle = Handle<BindlessTextureTag>;

    constexpr bool IsBindlessTextureTableCapacityValid(uint32_t capacity) noexcept
    {
        return capacity > 0 && capacity <= BindlessTextureTableLayout::max_capacity;
    }

    // Backend-private tables use a monotonically increasing submission serial
    // to quarantine released slots until all commands that could have sampled
    // their old entry are known complete. The serial is deliberately not a
    // native fence or queue type.
    using BindlessSubmissionSerial = uint64_t;

    struct BindlessTextureTableTelemetry
    {
        uint32_t capacity = 0;
        uint32_t allocated_slots = 0;
        uint32_t retired_slots = 0;
        uint32_t allocation_failures = 0;
    };

    // Pure slot-lifetime policy for a backend-owned bindless table. It does
    // not store descriptors, resident handles, or texture ownership. A
    // backend must retain the physical table entry until it calls
    // CollectCompleted with the serial supplied to Release.
    class BindlessTextureSlotAllocator
    {
    public:
        explicit BindlessTextureSlotAllocator(uint32_t capacity);

        BindlessTextureHandle Allocate();
        bool Release(BindlessTextureHandle handle,
                     BindlessSubmissionSerial last_submission_that_may_reference);
        void CollectCompleted(BindlessSubmissionSerial completed_submission);
        bool IsAllocated(BindlessTextureHandle handle) const noexcept;
        BindlessTextureTableTelemetry GetTelemetry() const noexcept;

    private:
        struct Slot
        {
            uint16_t generation = 0;
            bool allocated = false;
        };

        struct RetiredSlot
        {
            uint32_t id = KPENGINE_NULL_HANDLE;
            BindlessSubmissionSerial retire_after_submission = 0;
        };

        uint32_t capacity_ = 0;
        std::vector<Slot> slots_;
        std::vector<uint32_t> free_slots_;
        std::vector<RetiredSlot> retired_slots_;
        BindlessTextureTableTelemetry telemetry_{};
    };
}

#endif
