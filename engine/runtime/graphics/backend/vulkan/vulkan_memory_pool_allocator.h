#ifndef KEPNGINE_RUNTIME_GRAPHICS_VULKAN_MEMORY_POOL_ALLOCATOR_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_MEMORY_POOL_ALLOCATOR_H

#include <memory>
#include <vector>
#include <cstdint>
#include "vulkan_memory_allocator.h"
namespace kpengine::graphics
{

    class VulkanMemoryPoolAllocator : public IVulkanMemoryAllocator
    {
    public:
        virtual VulkanMemoryAllocation Allocate(VkDevice logicial_device, VkDeviceSize size, VkDeviceSize alignment, uint32_t memory_type_index) override;
        virtual void Free(VkDevice logicial_device, VulkanMemoryAllocation allocation) override;
        virtual void Destroy(VkDevice logicial_device) override;

    private:
        void CreateMemoryBlock(VkDevice logicial_device, VkDeviceSize block_size, VkDeviceSize slot_size, VkDeviceSize alignment, uint32_t memory_type_index);

    private:
        struct VulkanMemoryBlock
        {
            VkDeviceMemory memory;
            VkDeviceSize block_size;
            VkDeviceSize slot_size;
            uint32_t slot_count;
        };
        struct FreeSlot
        {
            uint32_t block_index;
            uint32_t slot_index;
        };
        struct VulkanMemoryPool
        {
            VkDeviceSize block_size;
            VkDeviceSize slot_size;
            std::vector<std::unique_ptr<VulkanMemoryBlock>> blocks_;
            std::vector<FreeSlot> free_slots_;
        };
    private:
        std::vector<VulkanMemoryPool> pools_;
    };
}

#endif