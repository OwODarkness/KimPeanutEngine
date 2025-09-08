#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_MEMORY_DEDICATED_ALLOCATOR_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_MEMORY_DEDICATED_ALLOCATOR_H

#include "vulkan_memory_allocator.h"
#include <vector>
namespace kpengine::graphics{
    class VulkanMemoryDedicatedAllocator : public IVulkanMemoryAllocator
    {
    public:
        VulkanMemoryAllocation Allocate(VkDevice logicial_device, VkDeviceSize size, VkDeviceSize alignment, uint32_t memory_type_index) override;
        void Free(VkDevice logicial_device, VulkanMemoryAllocation allocation) override;
        void Destroy(VkDevice logicial_device) override;      
    private:   
        std::vector<VkDeviceMemory> allocated_memory_blocks_;
    };
}

#endif