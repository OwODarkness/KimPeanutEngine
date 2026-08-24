#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_UPLOAD_CONTEXT_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_UPLOAD_CONTEXT_H

#include <cstddef>
#include <cstdint>
#include <vulkan/vulkan.h>

#include "common/api.h"
#include "common/texture.h"

namespace kpengine::graphics
{
    class VulkanDevice;
    class VulkanFrameContext;
    class VulkanBufferManager;

    // Synchronous upload helper. The context owns the staging/submit sequence,
    // while buffer memory remains owned by VulkanBufferManager/VulkanMemoryManager.
    class VulkanUploadContext
    {
    public:
        void Initialize(VulkanDevice *device, VulkanFrameContext *frame_context,
                        VulkanBufferManager *buffer_manager);

        void UploadBuffer(BufferHandle destination, size_t size, const void *data);
        void UploadTexture(VkImage image, const void *pixels, size_t pixel_size,
                           uint32_t width, uint32_t height, uint32_t mip_levels);

    private:
        BufferHandle CreateUploadStageBuffer(size_t size);
        VkCommandBuffer BeginOneShot(VkCommandPool command_pool);
        void SubmitAndRelease(VkCommandBuffer command_buffer, VkCommandPool command_pool,
                              VkQueue queue);

        VulkanDevice *device_ = nullptr;
        VulkanFrameContext *frame_context_ = nullptr;
        VulkanBufferManager *buffer_manager_ = nullptr;
    };
}

#endif
