#include "vulkan_memory_linear_allocator.h"
#include "log/logger.h"

namespace kpengine::graphics
{
    constexpr uint32_t DEFAULT_MEMORY_BLOCK_SIZE = 2 << 16;

    void LinearAllocator::Initialize(VkDevice logicial_device, VkDeviceSize size, uint32_t memory_type_index)
    {
        block_ = std::make_unique<MemoryBlock>();

        VkMemoryAllocateInfo memory_allocate_info{};
        memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memory_allocate_info.allocationSize = size;
        memory_allocate_info.memoryTypeIndex = memory_type_index;

        if (vkAllocateMemory(logicial_device, &memory_allocate_info, nullptr, &block_->memory) != VK_SUCCESS)
        {
            KP_LOG("MemoryAllocatorLog", LOG_LEVEL_ERROR, "Failed to allocate memory");
            throw std::runtime_error("Failed to allocate memory");
        }

        block_->size = size;
        block_->offset = 0;
    }

    MemoryAllocation LinearAllocator::Allocate(VkDevice logicial_device, VkDeviceSize size, VkDeviceSize alignment, uint32_t memory_type_index)
    {
        if (!block_)
        {
            Initialize(logicial_device, DEFAULT_MEMORY_BLOCK_SIZE, memory_type_index);
        }

        VkDeviceSize aligned_offset = (block_->offset + alignment - 1) & ~(alignment - 1);
        if (aligned_offset + size <= block_->size)
        {
            
            block_->offset = aligned_offset + size;
            MemoryAllocation allocate{};
            allocate.memory = block_->memory;
            allocate.size = size;
            allocate.offset = aligned_offset;
            allocate.owner = this; 
            return allocate;
        }
        else
        {
            KP_LOG("LinearAllocateLog", LOG_LEVEL_WARNNING, "allocate size is out of range");
            return {};
        }
    }

    void LinearAllocator::Free(VkDevice logicial_device, VkDeviceMemory memory)
    {

    }

    void LinearAllocator::Destroy(VkDevice logicial_device)
    {
        vkFreeMemory(logicial_device, block_->memory, nullptr);
        block_.reset();
    }

}