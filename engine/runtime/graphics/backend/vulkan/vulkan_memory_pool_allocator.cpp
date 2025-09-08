#include "vulkan_memory_pool_allocator.h"
#include "log/logger.h"

namespace kpengine::graphics
{
    constexpr VkDeviceSize pool_block_default_size = 1 << 26;
    constexpr VkDeviceSize pool_slot_default_size = 1 << 16;

    VulkanMemoryAllocation VulkanMemoryPoolAllocator::Allocate(VkDevice logicial_device, VkDeviceSize size, VkDeviceSize alignment, uint32_t memory_type_index)
    {
        if(free_slots_.empty())
        {
            CreateMemoryBlock(logicial_device, pool_block_default_size, pool_slot_default_size, alignment, memory_type_index);
        }

        FreeSlot slot = free_slots_.back();
        free_slots_.pop_back();
        const VulkanMemoryBlock* block= blocks_[slot.block_index].get();
        return {block->memory, block->slot_size, slot.slot_index * block->slot_size, slot.block_index, this};

    }
    void VulkanMemoryPoolAllocator::Free(VkDevice logicial_device, VulkanMemoryAllocation allocation)
    {
         uint32_t block_index = allocation.block_index;
        uint32_t slot_index = static_cast<uint32_t>(allocation.offset / blocks_[block_index]->slot_size);
        free_slots_.push_back({block_index, slot_index});
    }
    void VulkanMemoryPoolAllocator::Destroy(VkDevice logicial_device)
    {
        for(size_t i = 0;i<blocks_.size();i++)
        {

            vkFreeMemory(logicial_device, blocks_[i]->memory, nullptr);
        }
        
    }
    void VulkanMemoryPoolAllocator::CreateMemoryBlock(VkDevice logicial_device, VkDeviceSize block_size, VkDeviceSize slot_size, VkDeviceSize alignment, uint32_t memory_type_index)
    {
        VkDeviceSize adjust_slot_size = (slot_size + alignment - 1) & ~(alignment - 1);
        uint32_t pre_compute_slot_count =  static_cast<uint32_t>(block_size / adjust_slot_size);
        if(pre_compute_slot_count == 0)
        {
            KP_LOG("VulkanMemoryPoolAllocatorLog", LOG_LEVEL_WARNNING, "Failed to allocate memory,  the slot size is larger than block size");
        }

        blocks_.emplace_back(std::make_unique<VulkanMemoryBlock>());

        VkMemoryAllocateInfo memory_allocate_info{};
        memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memory_allocate_info.memoryTypeIndex = memory_type_index;
        memory_allocate_info.allocationSize = block_size;

        if(vkAllocateMemory(logicial_device, &memory_allocate_info, nullptr, &blocks_.back()->memory) != VK_SUCCESS)
        {
            KP_LOG("VulkanMemoryPoolAllocatorLog", LOG_LEVEL_ERROR, "Failed to allocate memory during using vkAllocateMemory");
            throw std::runtime_error("Failed to allocate memory");
        }

        blocks_.back()->block_size;
        blocks_.back()->slot_size = slot_size;
        blocks_.back()->slot_count = pre_compute_slot_count;
        //construct free slots
        for(uint32_t i = 0;i<pre_compute_slot_count;i++)
        {
            FreeSlot slot{static_cast<uint32_t>(blocks_.size() - 1), i};
            free_slots_.push_back(slot);
        }
    }

}