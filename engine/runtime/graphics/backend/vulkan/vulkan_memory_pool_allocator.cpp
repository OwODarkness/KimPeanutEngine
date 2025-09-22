#include "vulkan_memory_pool_allocator.h"
#include <array>
#include "log/logger.h"

namespace kpengine::graphics
{
    constexpr VkDeviceSize pool_block_default_size = 1 << 26;
    constexpr std::array<VkDeviceSize, 3> pool_slot_sizes = {1 << 12, 1 << 16, 1 << 22};

        VkDeviceSize VulkanMemoryPoolAllocator::GetMaxSupportedPoolSize() const
        {
            return pool_slot_sizes.back();
        }

    VulkanMemoryAllocation VulkanMemoryPoolAllocator::Allocate(VkDevice logicial_device, VkDeviceSize size, VkDeviceSize alignment, uint32_t memory_type_index)
    {
        if (size > pool_slot_sizes.back())
        {
            KP_LOG("VulkanMemoryPoolAllocatorLog", LOG_LEVEL_ERROR, "Failed to allocate memory, the max memory which pool supported is %d, but required %d", pool_slot_sizes.back(), size);
            throw std::runtime_error("Failed to allocate memory");
        }

        size_t best_fit_pool_index = UINT32_MAX;
        VkDeviceSize min_slot_size = UINT32_MAX;
        bool is_pool_found = false;
        for (size_t i = 0; i < pools_.size(); i++)
        {
            if (size <= pools_[i].slot_size && min_slot_size > pools_[i].slot_size && !pools_[i].free_slots_.empty())
            {
                best_fit_pool_index = i;
                min_slot_size = pools_[i].slot_size;
                is_pool_found = true;
            }
        }

        if (is_pool_found == false)
        {
            for (size_t i = 0; i < pool_slot_sizes.size(); i++)
            {
                if (size <= pool_slot_sizes[i])
                {
                    CreateMemoryBlock(logicial_device, pool_block_default_size, pool_slot_sizes[i], alignment, memory_type_index);
                    best_fit_pool_index = pools_.size() - 1;
                    break;
                }
            }
        }

        VulkanMemoryPool &pool = pools_[best_fit_pool_index];
        FreeSlot slot = pool.free_slots_.back();
        pool.free_slots_.pop_back();
        const VulkanMemoryBlock *block = pool.blocks_[slot.block_index].get();
        return {block->memory, block->slot_size, slot.slot_index * block->slot_size, slot.block_index, this};
    }
    void VulkanMemoryPoolAllocator::Free(VkDevice logicial_device, VulkanMemoryAllocation allocation)
    {
        for (auto &pool : pools_)
        {
            if (allocation.size == pool.slot_size)
            {
                uint32_t block_index = allocation.block_index;
                uint32_t slot_index = static_cast<uint32_t>(allocation.offset / pool.blocks_[block_index]->slot_size);
                pool.free_slots_.push_back({block_index, slot_index});
                break;
            }
        }
    }
    void VulkanMemoryPoolAllocator::Destroy(VkDevice logicial_device)
    {
        for (auto &pool : pools_)
        {
            for (size_t i = 0; i < pool.blocks_.size(); i++)
            {
                vkFreeMemory(logicial_device, pool.blocks_[i]->memory, nullptr);
            }
        }
    }
    void VulkanMemoryPoolAllocator::CreateMemoryBlock(VkDevice logicial_device, VkDeviceSize block_size, VkDeviceSize slot_size, VkDeviceSize alignment, uint32_t memory_type_index)
    {
        size_t pool_index{};
        bool is_pool_found = false;
        for (size_t i = 0; i < pools_.size(); i++)
        {
            if (pools_[i].slot_size == slot_size)
            {
                pool_index = i;
                is_pool_found = true;
                break;
            }
        }

        if (is_pool_found == false)
        {
            pools_.push_back({block_size, slot_size, {}, {}});
            pool_index = pools_.size() - 1;
        }

        VkDeviceSize adjust_slot_size = (slot_size + alignment - 1) & ~(alignment - 1);
        uint32_t pre_compute_slot_count = static_cast<uint32_t>(block_size / adjust_slot_size);
        if (pre_compute_slot_count == 0)
        {
            KP_LOG("VulkanMemoryPoolAllocatorLog", LOG_LEVEL_WARNNING, "Failed to allocate memory,  the slot size is larger than block size");
        }



        pools_[pool_index].blocks_.emplace_back(std::make_unique<VulkanMemoryBlock>());

        VkMemoryAllocateInfo memory_allocate_info{};
        memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memory_allocate_info.memoryTypeIndex = memory_type_index;
        memory_allocate_info.allocationSize = block_size;

        VulkanMemoryPool &pool = pools_[pool_index];

        if (vkAllocateMemory(logicial_device, &memory_allocate_info, nullptr, &pool.blocks_.back()->memory) != VK_SUCCESS)
        {
            KP_LOG("VulkanMemoryPoolAllocatorLog", LOG_LEVEL_ERROR, "Failed to allocate memory during using vkAllocateMemory");
            throw std::runtime_error("Failed to allocate memory");
        }

        pool.blocks_.back()->block_size = block_size;
        pool.blocks_.back()->slot_size = slot_size;
        pool.blocks_.back()->slot_count = pre_compute_slot_count;
        // construct free slots
        for (uint32_t i = 0; i < pre_compute_slot_count; i++)
        {
            FreeSlot slot{static_cast<uint32_t>(pool.blocks_.size() - 1), i};
            pool.free_slots_.push_back(slot);
        }
    }

}