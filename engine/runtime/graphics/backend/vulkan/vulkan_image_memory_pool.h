#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_IMAGE_MEMORY_POOL_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_IMAGE_MEMORY_POOL_H

#include "vulkan_memory_allocator.h"
#include <vulkan/vulkan.h>
#include <memory>
namespace kpengine::graphics
{

    class VulkanImageMemoryPool
    {
    public:
        VulkanImageMemoryPool();
        VulkanMemoryAllocation AllocateImageMemory(VkPhysicalDevice physical_device, VkDevice logical_device, VkImage image);
        void Free(VkDevice logical_device, const VulkanMemoryAllocation &allocation);
        void Destroy(VkDevice logical_device);

    private:
        uint32_t RequestMemoryTypeIndex(VkMemoryPropertyFlags memory_prop_flags, const VkMemoryRequirements &memory_require, const VkPhysicalDeviceMemoryProperties &physcial_memory_props);

    private:
        std::unique_ptr<IVulkanMemoryAllocator> pool_allocator_;
        std::unique_ptr<IVulkanMemoryAllocator> dedicated_allocator_;
    };
}

#endif