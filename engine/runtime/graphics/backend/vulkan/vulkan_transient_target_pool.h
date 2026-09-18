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
        // Destroys the reusable targets, so a description change -- a resize --
        // does not strand the previous size in the pool for the rest of the
        // session. Targets still outstanding or waiting on a submission serial
        // are left alone; they are trimmed by a later call. The caller must have
        // made submitted work safe first, as for DestroyAll.
        void DiscardReusable();

        uint32_t OutstandingCount() const noexcept
        {
            return static_cast<uint32_t>(outstanding_.size());
        }
        uint32_t RetiredCount() const noexcept
        {
            return static_cast<uint32_t>(retired_.size());
        }
        uint32_t ReusableCount() const noexcept
        {
            return static_cast<uint32_t>(reusable_.size());
        }
        // How often a description was handed a target other than the one it had
        // before. Reuse is identity-stable for a single consumer, which is what
        // keeps a caller's texture-handle-keyed caches valid; a non-zero count
        // means that assumption has stopped holding and cached descriptor sets
        // are being rebuilt per frame.
        uint32_t IdentityChangeCount() const noexcept { return identity_changes_; }

    private:
        struct Entry;

        static bool HasEntryFor(const std::vector<Entry> &entries, const RenderTargetDesc &desc);

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
        uint32_t identity_changes_ = 0;
    };
}

#endif
