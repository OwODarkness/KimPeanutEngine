#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_IMAGE_MEMORY_POOL_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_IMAGE_MEMORY_POOL_H

#include "vulkan_memory_pool_allocator.h"
#include <vulkan/vulkan.h>


namespace kpengine::graphics{
    class VulkanImageMemoryPool{
    public:
        VulkanMemoryAllocation AllocateImageMemory(VkPhysicalDevice physical_device, VkDevice logical_device, VkImage image);
        void Free(VkDevice logical_device, const VulkanMemoryAllocation& allocation);
        void Destroy(VkDevice logical_device);
    private:
    uint32_t RequestMemoryTypeIndex(VkMemoryPropertyFlags memory_prop_flags, const VkMemoryRequirements& memory_require, const VkPhysicalDeviceMemoryProperties& physcial_memory_props);
    private:
        std::unique_ptr<VulkanMemoryPoolAllocator> allocator_;
    };
}

#endif