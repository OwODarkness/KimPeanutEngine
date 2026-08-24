#include "vulkan_memory_pool_allocator.h"

#include <algorithm>
#include <stdexcept>

namespace kpengine::graphics
{
    VulkanMemoryAllocation VulkanMemoryPoolAllocator::Allocate(
        VkDevice logical_device, const VulkanMemoryAllocationRequest &request)
    {
        if (request.size == 0)
        {
            throw std::runtime_error("cannot suballocate an empty Vulkan memory range");
        }

        for (uint32_t index = 0; index < blocks_.size(); ++index)
        {
            const MemoryBlock *block = blocks_[index].get();
            if (!block || block->memory_type_index != request.memory_type_index ||
                block->properties != request.properties)
            {
                continue;
            }
            if (block->free_ranges.CanAllocate(request.size, request.alignment))
            {
                return AllocateFromBlock(index, request);
            }
        }

        const uint32_t block_index = CreateBlock(
            logical_device, request, std::max(kDefaultBlockSize, request.size));
        return AllocateFromBlock(block_index, request);
    }

    void VulkanMemoryPoolAllocator::Free(VkDevice logical_device,
                                         VulkanMemoryAllocation &allocation) noexcept
    {
        (void)logical_device;
        if (allocation.owner != this ||
            allocation.kind != VulkanMemoryAllocationKind::SharedBlock ||
            allocation.block_index >= blocks_.size())
        {
            allocation = {};
            return;
        }

        MemoryBlock *block = blocks_[allocation.block_index].get();
        if (!block || block->memory != allocation.memory)
        {
            allocation = {};
            return;
        }
        block->free_ranges.Free(allocation.offset, allocation.size);
        allocation = {};
    }

    void VulkanMemoryPoolAllocator::Destroy(VkDevice logical_device) noexcept
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

    uint32_t VulkanMemoryPoolAllocator::CreateBlock(
        VkDevice logical_device, const VulkanMemoryAllocationRequest &request,
        VkDeviceSize block_size)
    {
        auto block = std::make_unique<MemoryBlock>();
        block->size = block_size;
        block->memory_type_index = request.memory_type_index;
        block->properties = request.properties;

        VkMemoryAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate_info.allocationSize = block_size;
        allocate_info.memoryTypeIndex = request.memory_type_index;
        if (vkAllocateMemory(logical_device, &allocate_info, nullptr, &block->memory) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate a Vulkan shared memory block");
        }

        if ((request.properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0)
        {
            // A shared VkDeviceMemory block is mapped exactly once. Every
            // suballocation receives base + offset, preventing double mapping.
            void *mapped = nullptr;
            if (vkMapMemory(logical_device, block->memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS)
            {
                vkFreeMemory(logical_device, block->memory, nullptr);
                throw std::runtime_error("failed to persistently map a Vulkan shared memory block");
            }
            block->mapped_base = static_cast<std::byte *>(mapped);
        }
        block->free_ranges.Reset(block_size);
        blocks_.push_back(std::move(block));
        return static_cast<uint32_t>(blocks_.size() - 1);
    }

    VulkanMemoryAllocation VulkanMemoryPoolAllocator::AllocateFromBlock(
        uint32_t block_index, const VulkanMemoryAllocationRequest &request)
    {
        MemoryBlock *block = blocks_[block_index].get();
        if (!block)
        {
            throw std::runtime_error("invalid Vulkan shared memory block");
        }
        if (const std::optional<VkDeviceSize> offset =
                block->free_ranges.Allocate(request.size, request.alignment))
        {
            // Keep the block mapped; Free returns only this range to the pool.
            return {block->memory, *offset, request.size, block->size,
                    block->mapped_base ? block->mapped_base + *offset : nullptr,
                    block->memory_type_index, block->properties, block_index,
                    VulkanMemoryAllocationKind::SharedBlock, this};
        }
        throw std::runtime_error("Vulkan shared memory block has no compatible free range");
    }

    void VulkanMemoryPoolAllocator::ReleaseBlock(VkDevice logical_device,
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
