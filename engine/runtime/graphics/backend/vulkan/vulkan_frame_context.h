#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_FRAME_CONTEXT_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_FRAME_CONTEXT_H

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

#include "vulkan_device.h"
#include "common/texture.h"

namespace kpengine::graphics
{
    /**
     * The command/sync half of the Vulkan backend. Owns the command pools, the
     * per-frame scene command buffers, the semaphores/fences and the in-flight
     * index, plus the sync2 barrier. The backend delegates frame plumbing
     * (wait/acquire/reset/submit/present) here.
     */
    class VulkanFrameContext
    {
    public:
        static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 2;

        void Initialize(VulkanDevice *device, uint32_t swapchain_image_count);
        void Destroy();

        uint32_t GetCurrentFrameIndex() const { return current_frame_index_; }
        void AdvanceFrame() { current_frame_index_ = (current_frame_index_ + 1) % MAX_FRAMES_IN_FLIGHT; }

        VkCommandBuffer GetCurrentSceneCommandBuffer() const { return scene_command_buffers_[current_frame_index_]; }
        VkCommandPool GetGraphicsCommandPool() const { return graphics_command_pool_; }
        VkCommandPool GetTransferCommandPool() const { return transfer_command_pool_; }

        // frame plumbing (BeginFrame's wait/acquire/reset/submit/present)
        void WaitForInFlightFence();
        VkResult AcquireNextImage(VkSwapchainKHR swapchain, uint32_t &image_index);
        void ResetInFlightFence();
        void ResetCurrentSceneCommandBuffer();
        void Submit(VkCommandBuffer command_buffer, uint32_t image_index);
        VkResult Present(VkSwapchainKHR swapchain, uint32_t image_index);
        void OnSwapchainRecreated(uint32_t swapchain_image_count);

        // sync2 image barrier + TextureUsage-level transitions
        void TransitionImageLayout(VkCommandBuffer cmd, VkImage image, VkImageLayout old_layout, VkImageLayout new_layout, VkPipelineStageFlags2 src_stage, VkPipelineStageFlags2 dst_stage, VkAccessFlags2 src_access, VkAccessFlags2 dst_access, VkImageAspectFlags aspect_mask, uint32_t base_mip_level, uint32_t level_count);
        void TransitionImageLayout(VkCommandBuffer cmd, VkImage image, TextureUsage src_usage, TextureUsage dst_usage, uint32_t base_mip_level, uint32_t level_count);
        void CopyBufferToImage(VkCommandBuffer cmd, VkBuffer src_buffer, VkImage image, uint32_t width, uint32_t height);

    private:
        void CreateCommandPools();
        void CreateCommandBuffers();
        void CreateSyncObjects();

        VulkanDevice *device_ = nullptr;
        uint32_t swapchain_image_count_ = 0;

        VkCommandPool graphics_command_pool_ = VK_NULL_HANDLE;
        VkCommandPool transfer_command_pool_ = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer> scene_command_buffers_;
        std::vector<VkSemaphore> available_image_semaphores_;
        std::vector<VkSemaphore> render_finished_semaphores_;
        std::vector<VkFence> in_flight_fences_;
        uint32_t current_frame_index_ = 0;
    };
}

#endif
