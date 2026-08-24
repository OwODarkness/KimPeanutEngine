//VULKAN POOL USED FOR BUFFER

#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_BUFFER_MANAGER_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_BUFFER_MANAGER_H

#include <vector>
#include <vulkan/vulkan.h>
#include "common/api.h"
#include "vulkan_memory_manager.h"

namespace kpengine::graphics
{

    struct VulkanBufferResource
    {
        VkBuffer buffer;
        VulkanMemoryAllocation allocation;
        VkMemoryPropertyFlags mem_prop_flags;
        bool alive = false;
    };

    enum class VulkanMemoryUsageType
    {
        MEMORY_USAGE_STAGING,
        MEMORY_USAGE_DEVICE,
        MEMORY_USAGE_UNIFORM
    };


    class VulkanBufferManager
    {

    public:
        explicit VulkanBufferManager(VulkanMemoryManager &memory_manager);

        BufferHandle CreateBufferResource(VkDevice logical_device,
                                          const VkBufferCreateInfo *buffer_create_info,
                                          VulkanMemoryUsageType memory_type);
        bool DestroyBufferResource(VkDevice logical_device, BufferHandle handle);
        void DestroyAll(VkDevice logical_device);
        VulkanBufferResource *GetBufferResource(BufferHandle handle);
        void UploadData(BufferHandle handle, VkDeviceSize size, const void *src);
        void *GetMappedAddress(BufferHandle handle, VkDeviceSize size);

    private:
        std::vector<VulkanBufferResource> buffer_resources_;
        HandleSystem<BufferHandle> handle_system_;
        VulkanMemoryManager *memory_manager_ = nullptr;
    };
}

#endif
