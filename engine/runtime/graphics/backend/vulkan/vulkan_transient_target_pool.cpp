#include "vulkan_transient_target_pool.h"

#include <algorithm>
#include <cstddef>

#include "vulkan_frame_context.h"
#include "vulkan_render_target_manager.h"

namespace kpengine::graphics
{
    VulkanTransientTargetPool::VulkanTransientTargetPool(VulkanRenderTargetManager &targets,
                                                         VulkanFrameContext &frame_context)
        : targets_(&targets), frame_context_(&frame_context)
    {
    }

    VulkanTransientTargetPool::~VulkanTransientTargetPool()
    {
        // DestroyAll is the caller's contract, but a pool that is dropped with
        // targets still outstanding must not leak them.
        DestroyAll();
    }

    RenderTargetHandle VulkanTransientTargetPool::Acquire(const RenderTargetDesc &desc)
    {
        if (!targets_)
        {
            return {};
        }
        for (auto it = reusable_.begin(); it != reusable_.end(); ++it)
        {
            if (RenderTargetDescsShareStorage(it->desc, desc))
            {
                const RenderTargetHandle handle = it->handle;
                outstanding_.push_back(*it);
                reusable_.erase(it);
                return handle;
            }
        }
        Entry entry{targets_->Create(desc), desc};
        if (!entry.handle.IsValid())
        {
            return {};
        }
        outstanding_.push_back(entry);
        return entry.handle;
    }

    bool VulkanTransientTargetPool::Release(RenderTargetHandle handle)
    {
        if (!targets_)
        {
            return false;
        }
        const auto it = std::find_if(outstanding_.begin(), outstanding_.end(),
                                     [handle](const Entry &entry)
                                     { return entry.handle == handle; });
        if (it == outstanding_.end())
        {
            return false;
        }
        // The next submission is the newest thing that can reference the target,
        // whether or not a frame is currently open, so the pending serial is the
        // conservative stamp in both cases.
        const uint64_t retire_after =
            frame_context_ ? frame_context_->GetCurrentPendingSubmissionSerial() : 0;
        Entry entry = *it;
        entry.retire_after = retire_after;
        retired_.push_back(entry);
        outstanding_.erase(it);
        return true;
    }

    void VulkanTransientTargetPool::CollectCompleted(uint64_t completed_submission)
    {
        for (auto it = retired_.begin(); it != retired_.end();)
        {
            if (it->retire_after <= completed_submission)
            {
                reusable_.push_back(*it);
                it = retired_.erase(it);
                continue;
            }
            ++it;
        }
    }

    void VulkanTransientTargetPool::DestroyAll()
    {
        if (targets_)
        {
            for (const Entry &entry : outstanding_)
            {
                targets_->Destroy(entry.handle);
            }
            for (const Entry &entry : retired_)
            {
                targets_->Destroy(entry.handle);
            }
            for (const Entry &entry : reusable_)
            {
                targets_->Destroy(entry.handle);
            }
        }
        outstanding_.clear();
        retired_.clear();
        reusable_.clear();
    }
}
