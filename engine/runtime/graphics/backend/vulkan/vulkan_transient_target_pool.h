#ifndef KPENGINE_GRAPHICS_BACKEND_VULKAN_VULKAN_TRANSIENT_TARGET_POOL_H
#define KPENGINE_GRAPHICS_BACKEND_VULKAN_VULKAN_TRANSIENT_TARGET_POOL_H

#include <cstdint>
#include <vector>

#include "base/handle.h"
#include "common/render_target.h"

namespace kpengine::graphics
{
    class VulkanRenderTargetManager;
    class VulkanFrameContext;

    // Frame-safe reuse of render targets for bounded uses. A released target is
    // handed out again only once the submission that last referenced it has
    // completed, so a consumer never receives a target another frame may still
    // be writing, and never observes the previous consumer's contents.
    //
    // Reuse requires a matching description. The pool does not alias memory and
    // does not resize an existing target to fit a new description: a target is
    // destroyed and recreated when the description changes.
    class VulkanTransientTargetPool final
    {
    public:
        VulkanTransientTargetPool(VulkanRenderTargetManager &targets,
                                  VulkanFrameContext &frame_context);
        ~VulkanTransientTargetPool();

        VulkanTransientTargetPool(const VulkanTransientTargetPool &) = delete;
        VulkanTransientTargetPool &operator=(const VulkanTransientTargetPool &) = delete;

        // Hands out a target matching the description, reusing a released one
        // when the description matches and its last submission has completed.
        RenderTargetHandle Acquire(const RenderTargetDesc &desc);
        // Returns a target to the pool, stamped with the submission that may
        // still reference it. Returns false if the handle is not outstanding.
        bool Release(RenderTargetHandle handle);
        // Moves every target whose last referencing submission has completed
        // into the reusable set. `completed_submission` comes from the frame
        // context after a fence wait or an idle.
        void CollectCompleted(uint64_t completed_submission);
        // Destroys every target the pool holds. The caller must have made
        // submitted work safe first.
        void DestroyAll();

        uint32_t OutstandingCount() const noexcept { return outstanding_.size(); }
        uint32_t RetiredCount() const noexcept { return retired_.size(); }
        uint32_t ReusableCount() const noexcept { return reusable_.size(); }

    private:
        struct Entry
        {
            RenderTargetHandle handle;
            RenderTargetDesc desc;
            // Only meaningful while retired: the submission that may still
            // reference the target.
            uint64_t retire_after = 0;
        };


        VulkanRenderTargetManager *targets_ = nullptr;
        VulkanFrameContext *frame_context_ = nullptr;
        // Descriptions handed out and not yet released.
        std::vector<Entry> outstanding_;
        // Released, waiting for their last submission to complete.
        std::vector<Entry> retired_;
        // Released and safe to hand out again.
        std::vector<Entry> reusable_;
    };
}

#endif
