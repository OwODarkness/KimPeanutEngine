#include "vulkan_memory_dedicated_allocator.h"

#include <stdexcept>

namespace kpengine::graphics
{
    VulkanMemoryAllocation VulkanMemoryDedicatedAllocator::Allocate(
        VkDevice logical_device, const VulkanMemoryAllocationRequest &request)
    {
        if (request.size == 0)
        {
            throw std::runtime_error("cannot allocate empty dedicated Vulkan memory");
        }

        auto block = std::make_unique<MemoryBlock>();
        block->size = request.size;
        block->memory_type_index = request.memory_type_index;
        block->properties = request.properties;

        VkMemoryAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate_info.pNext = request.allocation_pnext;
        allocate_info.allocationSize = request.size;
        allocate_info.memoryTypeIndex = request.memory_type_index;
        if (vkAllocateMemory(logical_device, &allocate_info, nullptr, &block->memory) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate dedicated Vulkan memory");
        }

        if ((request.properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0)
        {
            // This allocation owns a distinct VkDeviceMemory object, but it is
            // still persistently mapped once for its complete lifetime.
            void *mapped = nullptr;
            if (vkMapMemory(logical_device, block->memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS)
            {
                vkFreeMemory(logical_device, block->memory, nullptr);
                throw std::runtime_error("failed to persistently map dedicated Vulkan memory");
            }
            block->mapped_base = static_cast<std::byte *>(mapped);
        }

        blocks_.push_back(std::move(block));
        const uint32_t block_index = static_cast<uint32_t>(blocks_.size() - 1);
        const MemoryBlock &allocated = *blocks_[block_index];
        return {allocated.memory, 0, allocated.size, allocated.size,
                allocated.mapped_base, allocated.memory_type_index,
                allocated.properties, block_index,
                VulkanMemoryAllocationKind::Dedicated, this};
    }

    void VulkanMemoryDedicatedAllocator::Free(VkDevice logical_device,
                                              VulkanMemoryAllocation &allocation) noexcept
    {
        if (allocation.owner != this ||
            allocation.kind != VulkanMemoryAllocationKind::Dedicated ||
            allocation.block_index >= blocks_.size())
        {
            allocation = {};
            return;
        }

        std::unique_ptr<MemoryBlock> &block = blocks_[allocation.block_index];
        if (!block || block->memory != allocation.memory)
        {
            allocation = {};
            return;
        }
        ReleaseBlock(logical_device, *block);
        block.reset();
        allocation = {};
    }

    void VulkanMemoryDedicatedAllocator::Destroy(VkDevice logical_device) noexcept
    {
        for (const std::unique_ptr<MemoryBlock> &block : blocks_)
        {
            if (block)
            {
                ReleaseBlock(logical_device, *block);
            }
        }
        blocks_.clear();
    }

    void VulkanMemoryDedicatedAllocator::ReleaseBlock(VkDevice logical_device,
                                                       MemoryBlock &block) noexcept
    {
        if (block.mapped_base)
        {
            vkUnmapMemory(logical_device, block.memory);
            block.mapped_base = nullptr;
        }
        if (block.memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(logical_device, block.memory, nullptr);
            block.memory = VK_NULL_HANDLE;
        }
    }
}
