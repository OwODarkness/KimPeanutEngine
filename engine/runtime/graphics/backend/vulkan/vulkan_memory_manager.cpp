#include "vulkan_memory_manager.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

namespace kpengine::graphics
{
    VulkanMemoryManager::VulkanMemoryManager(VkPhysicalDevice physical_device,
                                             VkDevice logical_device)
        : physical_device_(physical_device), logical_device_(logical_device)
    {
        vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory_properties_);

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical_device_, &properties);
        non_coherent_atom_size_ = std::max<VkDeviceSize>(
            1, properties.limits.nonCoherentAtomSize);
    }

    VulkanMemoryManager::~VulkanMemoryManager()
    {
        Destroy();
    }

    VulkanBufferMemoryAllocation VulkanMemoryManager::Allocate(
        const VkMemoryRequirements &requirements, VkMemoryPropertyFlags required_properties,
        VulkanMemoryAllocationPolicy policy,
        const VkMemoryDedicatedAllocateInfo *dedicated_info)
    {
        if (destroyed_ || requirements.size == 0)
        {
            throw std::runtime_error("cannot allocate Vulkan memory from a destroyed manager");
        }

        const uint32_t memory_type_index =
            FindMemoryType(requirements.memoryTypeBits, required_properties);
        if (policy == VulkanMemoryAllocationPolicy::Dedicated ||
            requirements.size > dedicated_threshold_)
        {
            const uint32_t block_index = CreateBlock(requirements.size, memory_type_index,
                                                     required_properties, true, dedicated_info);
            return AllocateFromBlock(block_index, requirements, true);
        }

        for (uint32_t index = 0; index < shared_blocks_.size(); ++index)
        {
            const MemoryBlock *block = shared_blocks_[index].get();
            if (block && block->memory_type_index == memory_type_index &&
                block->properties == required_properties)
            {
                for (const FreeRange &range : block->free_ranges)
                {
                    const VkDeviceSize offset = AlignUp(range.offset, requirements.alignment);
                    if (offset >= range.offset &&
                        requirements.size <= range.size - (offset - range.offset))
                    {
                        return AllocateFromBlock(index, requirements, false);
                    }
                }
            }
        }

        const VkDeviceSize block_size = std::max(shared_block_size_, requirements.size);
        const uint32_t block_index = CreateBlock(block_size, memory_type_index,
                                                 required_properties, false, nullptr);
        return AllocateFromBlock(block_index, requirements, false);
    }

    void VulkanMemoryManager::Free(VulkanBufferMemoryAllocation &allocation) noexcept
    {
        MemoryBlock *block = FindBlock(allocation);
        if (!block)
        {
            allocation = {};
            return;
        }

        if (allocation.dedicated)
        {
            ReleaseBlock(*block);
            dedicated_blocks_[allocation.block_index].reset();
        }
        else
        {
            block->free_ranges.push_back({allocation.offset, allocation.size});
            MergeFreeRanges(*block);
        }
        allocation = {};
    }

    void VulkanMemoryManager::Write(const VulkanBufferMemoryAllocation &allocation,
                                    const void *source, VkDeviceSize size,
                                    VkDeviceSize offset)
    {
        if (!source || !allocation.mapped_address || offset > allocation.size ||
            size > allocation.size - offset)
        {
            throw std::runtime_error("invalid host write to Vulkan memory allocation");
        }
        std::memcpy(allocation.mapped_address + offset, source,
                    static_cast<size_t>(size));
        Flush(allocation, size, offset);
    }

    void VulkanMemoryManager::Flush(const VulkanBufferMemoryAllocation &allocation,
                                    VkDeviceSize size, VkDeviceSize offset)
    {
        FlushOrInvalidate(allocation, size, offset, true);
    }

    void VulkanMemoryManager::Invalidate(const VulkanBufferMemoryAllocation &allocation,
                                         VkDeviceSize size, VkDeviceSize offset)
    {
        FlushOrInvalidate(allocation, size, offset, false);
    }

    void VulkanMemoryManager::Destroy() noexcept
    {
        if (destroyed_)
        {
            return;
        }
        for (const std::unique_ptr<MemoryBlock> &block : shared_blocks_)
        {
            if (block)
            {
                ReleaseBlock(*block);
            }
        }
        for (const std::unique_ptr<MemoryBlock> &block : dedicated_blocks_)
        {
            if (block)
            {
                ReleaseBlock(*block);
            }
        }
        shared_blocks_.clear();
        dedicated_blocks_.clear();
        destroyed_ = true;
    }

    VkDeviceSize VulkanMemoryManager::AlignUp(VkDeviceSize value, VkDeviceSize alignment)
    {
        return alignment == 0 ? value : (value + alignment - 1) / alignment * alignment;
    }

    uint32_t VulkanMemoryManager::FindMemoryType(
        uint32_t memory_type_bits, VkMemoryPropertyFlags required_properties) const
    {
        for (uint32_t index = 0; index < memory_properties_.memoryTypeCount; ++index)
        {
            if ((memory_type_bits & (1u << index)) != 0 &&
                (memory_properties_.memoryTypes[index].propertyFlags & required_properties) ==
                    required_properties)
            {
                return index;
            }
        }
        throw std::runtime_error("failed to find a compatible Vulkan memory type");
    }

    uint32_t VulkanMemoryManager::CreateBlock(VkDeviceSize size, uint32_t memory_type_index,
                                              VkMemoryPropertyFlags properties, bool dedicated,
                                              const VkMemoryDedicatedAllocateInfo *dedicated_info)
    {
        auto block = std::make_unique<MemoryBlock>();
        block->size = size;
        block->memory_type_index = memory_type_index;
        block->properties = properties;

        VkMemoryAllocateInfo allocate_info{};
        allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate_info.pNext = dedicated_info;
        allocate_info.allocationSize = size;
        allocate_info.memoryTypeIndex = memory_type_index;
        if (vkAllocateMemory(logical_device_, &allocate_info, nullptr, &block->memory) != VK_SUCCESS)
        {
            throw std::runtime_error("failed to allocate Vulkan device memory");
        }

        if ((properties & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0)
        {
            void *mapped = nullptr;
            if (vkMapMemory(logical_device_, block->memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS)
            {
                vkFreeMemory(logical_device_, block->memory, nullptr);
                throw std::runtime_error("failed to persistently map Vulkan device memory");
            }
            block->mapped_base = static_cast<std::byte *>(mapped);
        }
        block->free_ranges.push_back({0, size});

        if (!dedicated)
        {
            shared_blocks_.push_back(std::move(block));
            return static_cast<uint32_t>(shared_blocks_.size() - 1);
        }
        dedicated_blocks_.push_back(std::move(block));
        return static_cast<uint32_t>(dedicated_blocks_.size() - 1);
    }

    VulkanBufferMemoryAllocation VulkanMemoryManager::AllocateFromBlock(
        uint32_t block_index, const VkMemoryRequirements &requirements, bool dedicated)
    {
        MemoryBlock *block = dedicated ? dedicated_blocks_[block_index].get()
                                       : shared_blocks_[block_index].get();
        if (!block)
        {
            throw std::runtime_error("invalid Vulkan memory block");
        }

        for (size_t index = 0; index < block->free_ranges.size(); ++index)
        {
            const FreeRange range = block->free_ranges[index];
            const VkDeviceSize offset = AlignUp(range.offset, requirements.alignment);
            const VkDeviceSize prefix = offset - range.offset;
            if (prefix > range.size || requirements.size > range.size - prefix)
            {
                continue;
            }

            const VkDeviceSize suffix = range.size - prefix - requirements.size;
            block->free_ranges.erase(block->free_ranges.begin() + index);
            if (prefix != 0)
            {
                block->free_ranges.push_back({range.offset, prefix});
            }
            if (suffix != 0)
            {
                block->free_ranges.push_back({offset + requirements.size, suffix});
            }
            return {block->memory, offset, requirements.size,
                    block->mapped_base ? block->mapped_base + offset : nullptr,
                    block_index, dedicated};
        }
        throw std::runtime_error("Vulkan memory block has no compatible free range");
    }

    VulkanMemoryManager::MemoryBlock *VulkanMemoryManager::FindBlock(
        const VulkanBufferMemoryAllocation &allocation) noexcept
    {
        std::vector<std::unique_ptr<MemoryBlock>> &blocks = allocation.dedicated
            ? dedicated_blocks_ : shared_blocks_;
        if (!allocation.IsValid() || allocation.block_index >= blocks.size())
        {
            return nullptr;
        }
        MemoryBlock *block = blocks[allocation.block_index].get();
        return block && block->memory == allocation.memory ? block : nullptr;
    }

    const VulkanMemoryManager::MemoryBlock *VulkanMemoryManager::FindBlock(
        const VulkanBufferMemoryAllocation &allocation) const noexcept
    {
        const std::vector<std::unique_ptr<MemoryBlock>> &blocks = allocation.dedicated
            ? dedicated_blocks_ : shared_blocks_;
        if (!allocation.IsValid() || allocation.block_index >= blocks.size())
        {
            return nullptr;
        }
        const MemoryBlock *block = blocks[allocation.block_index].get();
        return block && block->memory == allocation.memory ? block : nullptr;
    }

    void VulkanMemoryManager::ReleaseBlock(MemoryBlock &block) noexcept
    {
        if (block.mapped_base)
        {
            vkUnmapMemory(logical_device_, block.memory);
            block.mapped_base = nullptr;
        }
        if (block.memory != VK_NULL_HANDLE)
        {
            vkFreeMemory(logical_device_, block.memory, nullptr);
            block.memory = VK_NULL_HANDLE;
        }
    }

    void VulkanMemoryManager::MergeFreeRanges(MemoryBlock &block)
    {
        std::sort(block.free_ranges.begin(), block.free_ranges.end(),
                  [](const FreeRange &left, const FreeRange &right)
                  { return left.offset < right.offset; });
        std::vector<FreeRange> merged;
        for (const FreeRange &range : block.free_ranges)
        {
            if (!merged.empty() && merged.back().offset + merged.back().size == range.offset)
            {
                merged.back().size += range.size;
            }
            else
            {
                merged.push_back(range);
            }
        }
        block.free_ranges = std::move(merged);
    }

    void VulkanMemoryManager::FlushOrInvalidate(
        const VulkanBufferMemoryAllocation &allocation, VkDeviceSize size,
        VkDeviceSize offset, bool flush)
    {
        const MemoryBlock *block = FindBlock(allocation);
        if (!block || !allocation.mapped_address || offset > allocation.size ||
            size > allocation.size - offset)
        {
            throw std::runtime_error("invalid Vulkan memory synchronization range");
        }
        if ((block->properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0)
        {
            return;
        }

        const VkDeviceSize allocation_offset = allocation.offset + offset;
        const VkDeviceSize aligned_offset = allocation_offset / non_coherent_atom_size_ *
                                            non_coherent_atom_size_;
        const VkDeviceSize aligned_end = AlignUp(allocation_offset + size,
                                                 non_coherent_atom_size_);
        VkMappedMemoryRange range{};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = allocation.memory;
        range.offset = aligned_offset;
        range.size = std::min(aligned_end - aligned_offset, block->size - aligned_offset);

        const VkResult result = flush
            ? vkFlushMappedMemoryRanges(logical_device_, 1, &range)
            : vkInvalidateMappedMemoryRanges(logical_device_, 1, &range);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to synchronize non-coherent Vulkan memory");
        }
    }
}
