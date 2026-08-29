#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_RENDER_TARGET_READBACK_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_RENDER_TARGET_READBACK_H

#include <mutex>
#include <vector>

#include <vulkan/vulkan.h>

#include "common/render_target_readback.h"

namespace kpengine::graphics
{
    class VulkanBufferManager;
    class VulkanFrameContext;
    class VulkanRenderTargetManager;

    // Vulkan-private readback service. It owns native copy recording,
    // readback-buffer lifetime, pending requests, and common callbacks.
    class VulkanRenderTargetReadback final : public IRenderTargetReadback
    {
    public:
        VulkanRenderTargetReadback(VkDevice logical_device, VulkanFrameContext &frame_context,
                                   VulkanBufferManager &buffer_manager,
                                   VulkanRenderTargetManager &render_target_manager);

        bool EnqueueRenderTargetReadback(RenderTargetReadbackRequest request,
                                         RenderTargetReadbackCallback on_completed) override;
        void RecordPendingCopies(VkCommandBuffer command_buffer, uint64_t submission_serial);
        void CollectCompleted(uint64_t completed_submission_serial);
        void CollectCompletedReadbacks() override;
        void CancelTarget(RenderTargetHandle target, std::string diagnostic);
        void DrainPendingReadbacks(std::string diagnostic) override;

    private:
        struct PendingReadback
        {
            RenderTargetReadbackRequest request;
            RenderTargetReadbackCallback on_completed;
            BufferHandle staging_buffer;
            uint64_t submission_serial = 0;
        };

        void CompleteCancelled(std::vector<PendingReadback> pending, const std::string &diagnostic);

        VkDevice logical_device_ = VK_NULL_HANDLE;
        VulkanFrameContext *frame_context_ = nullptr;
        VulkanBufferManager *buffer_manager_ = nullptr;
        VulkanRenderTargetManager *render_target_manager_ = nullptr;
        std::mutex mutex_;
        std::vector<PendingReadback> queued_;
        std::vector<PendingReadback> submitted_;
    };
}

#endif
