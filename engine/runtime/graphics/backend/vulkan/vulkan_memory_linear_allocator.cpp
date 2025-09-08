#include "vulkan_memory_linear_allocator.h"
#include "log/logger.h"

namespace kpengine::graphics
{
    constexpr uint32_t DEFAULT_MEMORY_BLOCK_SIZE = 2 << 16;

    void VulkanMemoryLinearAllocator::CreateNewBlock(VkDevice logicial_device, VkDeviceSize size, uint32_t memory_type_index)
    {
        blocks_.push_back(std::make_unique<VulkanMemoryBlock>());

        VkMemoryAllocateInfo memory_allocate_info{};
        memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memory_allocate_info.allocationSize = size;
        memory_allocate_info.memoryTypeIndex = memory_type_index;

        if (vkAllocateMemory(logicial_device, &memory_allocate_info, nullptr, &blocks_.back()->memory) != VK_SUCCESS)
        {
            KP_LOG("MemoryAllocatorLog", LOG_LEVEL_ERROR, "Failed to allocate memory");
            throw std::runtime_error("Failed to allocate memory");
        }

        blocks_.back()->size = size;
        blocks_.back()->offset = 0;
    }

    VulkanMemoryAllocation VulkanMemoryLinearAllocator::Allocate(VkDevice logicial_device, VkDeviceSize size, VkDeviceSize alignment, uint32_t memory_type_index)
    {
        for(size_t i = 0;i<blocks_.size();i++)
        {

            VkDeviceSize aligned_offset = (blocks_[i]->offset + alignment - 1) & ~(alignment - 1);
            if (aligned_offset + size <= blocks_[i]->size)
            {

                blocks_[i]->offset = aligned_offset + size;
                return {blocks_[i]->memory, size, aligned_offset, static_cast<uint32_t>(i),  this};
            }
        }

        const VkDeviceSize k_pre_allocated_block_size = DEFAULT_MEMORY_BLOCK_SIZE;
        CreateNewBlock(logicial_device, DEFAULT_MEMORY_BLOCK_SIZE, memory_type_index);
        VkDeviceSize aligned_offset = (blocks_.back()->offset + alignment - 1) & ~(alignment - 1);
        blocks_.back()->offset = aligned_offset + size;
        
        return {blocks_.back()->memory, size, aligned_offset, static_cast<uint32_t>(blocks_.size() - 1) ,this};
    }

    void VulkanMemoryLinearAllocator::Free(VkDevice logicial_device, VulkanMemoryAllocation allocation)
    {
    }

    void VulkanMemoryLinearAllocator::Destroy(VkDevice logicial_device)
    {
        for (size_t i = 0; i < blocks_.size(); i++)
        {
            vkFreeMemory(logicial_device, blocks_[i]->memory, nullptr);
            blocks_[i].reset();
        }
    }

}