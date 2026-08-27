#include "bindless_texture.h"

namespace kpengine::graphics
{
    BindlessTextureSlotAllocator::BindlessTextureSlotAllocator(uint32_t capacity)
        : capacity_(IsBindlessTextureTableCapacityValid(capacity) ? capacity : 0)
    {
    }

    BindlessTextureHandle BindlessTextureSlotAllocator::Allocate()
    {
        uint32_t slot = KPENGINE_NULL_HANDLE;
        if (!free_slots_.empty())
        {
            slot = free_slots_.back();
            free_slots_.pop_back();
        }
        else if (slots_.size() < capacity_)
        {
            slot = static_cast<uint32_t>(slots_.size());
            slots_.push_back({});
        }
        else
        {
            ++telemetry_.allocation_failures;
            return {};
        }

        Slot &state = slots_[slot];
        state.allocated = true;
        ++telemetry_.allocated_slots;
        return {slot, state.generation};
    }

    bool BindlessTextureSlotAllocator::Release(
        BindlessTextureHandle handle,
        BindlessSubmissionSerial last_submission_that_may_reference)
    {
        if (!IsAllocated(handle))
        {
            return false;
        }

        Slot &state = slots_[handle.id];
        state.allocated = false;
        --telemetry_.allocated_slots;
        ++state.generation;
        ++telemetry_.retired_slots;
        retired_slots_.push_back({handle.id, last_submission_that_may_reference});
        return true;
    }

    void BindlessTextureSlotAllocator::CollectCompleted(
        BindlessSubmissionSerial completed_submission)
    {
        size_t retained_begin = 0;
        for (size_t index = 0; index < retired_slots_.size(); ++index)
        {
            const RetiredSlot retired = retired_slots_[index];
            if (retired.retire_after_submission <= completed_submission)
            {
                Slot &state = slots_[retired.id];
                --telemetry_.retired_slots;
                // Generation zero would let a stale first-generation handle
                // alias this slot after wraparound. Quarantine it permanently.
                if (state.generation != 0)
                {
                    free_slots_.push_back(retired.id);
                }
            }
            else
            {
                retired_slots_[retained_begin++] = retired;
            }
        }
        retired_slots_.resize(retained_begin);
    }

    bool BindlessTextureSlotAllocator::IsAllocated(BindlessTextureHandle handle) const noexcept
    {
        return handle.IsValid() && handle.id < slots_.size() &&
               slots_[handle.id].allocated &&
               slots_[handle.id].generation == handle.generation;
    }

    BindlessTextureTableTelemetry BindlessTextureSlotAllocator::GetTelemetry() const noexcept
    {
        BindlessTextureTableTelemetry telemetry = telemetry_;
        telemetry.capacity = capacity_;
        return telemetry;
    }
}
