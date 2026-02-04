#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_MEMORY_ALLOCATOR_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_MEMORY_ALLOCATOR_H

#include <vulkan/vulkan.h>

namespace kpengine::graphics
{
    /**
     * metadata of allocation
     */
    struct VulkanMemoryAllocation
    {
        VkDeviceMemory memory;
        VkDeviceSize size;
        VkDeviceSize offset;
        uint32_t block_index;
        class IVulkanMemoryAllocator *owner = nullptr; //trace which allocator it belong to
    };


    class IVulkanMemoryAllocator
    {
    public:
        virtual VulkanMemoryAllocation Allocate(VkDevice logicial_device, VkDeviceSize size, VkDeviceSize alignment, uint32_t memory_type_index) = 0;
        //free() clean up the data of correspond memory, but keep the memory space
        virtual void Free(VkDevice logicial_device, VulkanMemoryAllocation allocation) = 0;
        virtual void Destroy(VkDevice logicial_device) = 0;
    };
}

#endif