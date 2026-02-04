#ifndef KEPNGINE_RUNTIME_GRAPHICS_VULKAN_MEMORY_POOL_ALLOCATOR_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_MEMORY_POOL_ALLOCATOR_H

#include <memory>
#include <vector>
#include <cstdint>
#include "vulkan_memory_allocator.h"
namespace kpengine::graphics
{
    /**
     * Pool based memory allocator for Vulkan
     */
    class VulkanMemoryPoolAllocator : public IVulkanMemoryAllocator
    {
    public:
        virtual VulkanMemoryAllocation Allocate(VkDevice logicial_device, VkDeviceSize size, VkDeviceSize alignment, uint32_t memory_type_index) override;
        virtual void Free(VkDevice logicial_device, VulkanMemoryAllocation allocation) override;
        virtual void Destroy(VkDevice logicial_device) override;
        VkDeviceSize GetMaxSupportedPoolSize() const;

    private:
        void CreateMemoryBlock(VkDevice logicial_device, VkDeviceSize block_size, VkDeviceSize slot_size, VkDeviceSize alignment, uint32_t memory_type_index);

    private:
        // Memory block: VkDeviceMemory divided into fixed slots
        struct VulkanMemoryBlock
        {
            VkDeviceMemory memory;
            VkDeviceSize block_size;
            VkDeviceSize slot_size;
            uint32_t slot_count;
        };

        struct FreeSlot
        {
            uint32_t block_index; // Index in pool's blocks_ vector
            uint32_t slot_index;  // Slot index within block (0 to slot_count-1)
        };

        // Memory pool: Collection of blocks with same slot size
        struct VulkanMemoryPool
        {
            VkDeviceSize block_size;
            VkDeviceSize slot_size;
            std::vector<std::unique_ptr<VulkanMemoryBlock>> blocks_; // Owned blocks
            std::vector<FreeSlot> free_slots_;                       // Available slots
        };

    private:
        // the slot size of pool could be different
        std::vector<VulkanMemoryPool> pools_;
    };
}

#endif