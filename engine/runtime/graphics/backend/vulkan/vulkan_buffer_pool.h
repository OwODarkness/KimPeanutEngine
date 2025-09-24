#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_BUFFER_POOL_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_BUFFER_POOL_H

#include <vector>
#include <memory>
#include <unordered_map>
#include <vulkan/vulkan.h>
#include "common/api.h"
#include "vulkan_memory_allocator.h"

namespace kpengine::graphics
{
    struct VulkanBufferResource
    {
        VkBuffer buffer;
        VulkanMemoryAllocation allocation;
        VkMemoryPropertyFlags mem_prop_flags;
        bool alive = false;
    };

    enum class VulkanMemoryUsageType{
        MEMORY_USAGE_STAGING,
        MEMORY_USAGE_DEVICE,
        MEMORY_USAGE_UNIFORM
    };
    
    class VulkanBufferPool
    {

    public:
        BufferHandle CreateBufferResource(VkPhysicalDevice physical_device, VkDevice logical_device, const VkBufferCreateInfo *buffer_create_info, VulkanMemoryUsageType memory_type);
        bool DestroyBufferResource(VkDevice logical_device, BufferHandle handle);
        VulkanBufferResource *GetBufferResource(BufferHandle handle);
        //upload data in src into gpu memory
        void UploadData(VkDevice logical_device, BufferHandle handle, VkDeviceSize size, const void *src);
        void MapBuffer(VkDevice logical_device, BufferHandle handle, VkDeviceSize size, void** mapped_ptr);
        void FreeMemory(VkDevice logicial_device);

    private:
        uint32_t RequestMemoryTypeIndex(VkMemoryPropertyFlags memory_prop_flags, const VkMemoryRequirements& memory_require, const VkPhysicalDeviceMemoryProperties);

    private:
        std::vector<VulkanBufferResource> buffer_resources_;
        HandleSystem<BufferHandle> handle_system_;
        std::unordered_map<VulkanMemoryUsageType, std::unique_ptr<IVulkanMemoryAllocator>> memory_allocators_;
        std::unordered_map<VulkanMemoryUsageType, std::unique_ptr<IVulkanMemoryAllocator>> dedicated_allocators_;
        VkDeviceSize pool_max_size = 1 << 22;
    };
}


#endif