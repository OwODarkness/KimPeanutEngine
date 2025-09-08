#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_MEMORY_LINEAR_ALLOCATOR_H
#define kPENGINE_RUTNIME_GRAPHICS_VULKAN_MEMORY_LINEAR_ALLOCATOR_H

#include <vector>
#include <memory>
#include "vulkan_memory_allocator.h"

namespace kpengine::graphics
{

    class VulkanMemoryLinearAllocator : public IVulkanMemoryAllocator
    {
    public:
        VulkanMemoryAllocation Allocate(VkDevice logicial_device, VkDeviceSize size, VkDeviceSize alignment, uint32_t memory_type_index) override;
        void Free(VkDevice logicial_device, VulkanMemoryAllocation allocation) override;
        void Destroy(VkDevice logicial_device) override;

    private:
        void CreateNewBlock(VkDevice logicial_device, VkDeviceSize size, uint32_t memory_type_index);

    private:
        struct VulkanMemoryBlock
        {
            VkDeviceMemory memory;
            VkDeviceSize size;
            VkDeviceSize offset;
        };

    private:
        std::vector<std::unique_ptr<VulkanMemoryBlock>> blocks_;
    };
}

#endif