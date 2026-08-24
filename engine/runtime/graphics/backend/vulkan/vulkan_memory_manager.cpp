#include "vulkan_memory_manager.h"

#include <algorithm>
#include <cstring>
#include <stdexcept>

#include "vulkan_memory_dedicated_allocator.h"
#include "vulkan_memory_pool_allocator.h"

namespace kpengine::graphics
{
    VulkanMemoryManager::VulkanMemoryManager(VkPhysicalDevice physical_device,
                                             VkDevice logical_device)
        : logical_device_(logical_device),
          shared_allocator_(std::make_unique<VulkanMemoryPoolAllocator>()),
          dedicated_allocator_(std::make_unique<VulkanMemoryDedicatedAllocator>())
    {
        vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_properties_);

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(physical_device, &properties);
        non_coherent_atom_size_ = std::max<VkDeviceSize>(
            1, properties.limits.nonCoherentAtomSize);
    }

    VulkanMemoryManager::~VulkanMemoryManager()
    {
        Destroy();
    }

    VulkanMemoryAllocation VulkanMemoryManager::Allocate(
        const VkMemoryRequirements &requirements,
        VkMemoryPropertyFlags required_properties,
        VulkanMemoryAllocationPolicy policy,
        const VkMemoryDedicatedAllocateInfo *dedicated_info)
    {
        if (destroyed_ || requirements.size == 0)
        {
            throw std::runtime_error("cannot allocate Vulkan memory from a destroyed manager");
        }

        // Large allocations avoid fragmenting shared blocks even when Vulkan does
        // not explicitly require dedicated memory.
        const bool use_dedicated = policy == VulkanMemoryAllocationPolicy::Dedicated ||
                                   requirements.size > dedicated_threshold_;
        const VulkanMemoryAllocationRequest request{
            requirements.size,
            requirements.alignment,
            FindMemoryType(requirements.memoryTypeBits, required_properties),
            required_properties,
            use_dedicated ? dedicated_info : nullptr,
        };
        IVulkanMemoryAllocator *const allocator = use_dedicated
            ? dedicated_allocator_.get() : shared_allocator_.get();
        return allocator->Allocate(logical_device_, request);
    }

    void VulkanMemoryManager::Free(VulkanMemoryAllocation &allocation) noexcept
    {
        if (allocation.owner)
        {
            allocation.owner->Free(logical_device_, allocation);
        }
        else
        {
            allocation = {};
        }
    }

    void VulkanMemoryManager::Write(const VulkanMemoryAllocation &allocation,
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

    void VulkanMemoryManager::Flush(const VulkanMemoryAllocation &allocation,
                                    VkDeviceSize size, VkDeviceSize offset)
    {
        FlushOrInvalidate(allocation, size, offset, true);
    }

    void VulkanMemoryManager::Invalidate(const VulkanMemoryAllocation &allocation,
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
        if (shared_allocator_)
        {
            shared_allocator_->Destroy(logical_device_);
        }
        if (dedicated_allocator_)
        {
            dedicated_allocator_->Destroy(logical_device_);
        }
        destroyed_ = true;
    }

    VkDeviceSize VulkanMemoryManager::AlignUp(VkDeviceSize value,
                                               VkDeviceSize alignment)
    {
        return alignment == 0 ? value : (value + alignment - 1) / alignment * alignment;
    }

    uint32_t VulkanMemoryManager::FindMemoryType(
        uint32_t memory_type_bits,
        VkMemoryPropertyFlags required_properties) const
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

    void VulkanMemoryManager::FlushOrInvalidate(
        const VulkanMemoryAllocation &allocation,
        VkDeviceSize size, VkDeviceSize offset, bool flush)
    {
        if (!allocation.IsValid() || !allocation.mapped_address ||
            offset > allocation.size || size > allocation.size - offset)
        {
            throw std::runtime_error("invalid Vulkan memory synchronization range");
        }
        if ((allocation.properties & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) != 0)
        {
            return;
        }

        // Vulkan requires non-coherent ranges to cover whole atom boundaries;
        // otherwise bytes adjacent to this suballocation may remain invisible.
        const VkDeviceSize allocation_offset = allocation.offset + offset;
        const VkDeviceSize aligned_offset = allocation_offset / non_coherent_atom_size_ *
                                            non_coherent_atom_size_;
        const VkDeviceSize aligned_end = AlignUp(allocation_offset + size,
                                                 non_coherent_atom_size_);
        VkMappedMemoryRange range{};
        range.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = allocation.memory;
        range.offset = aligned_offset;
        range.size = std::min(aligned_end - aligned_offset,
                              allocation.backing_size - aligned_offset);

        const VkResult result = flush
            ? vkFlushMappedMemoryRanges(logical_device_, 1, &range)
            : vkInvalidateMappedMemoryRanges(logical_device_, 1, &range);
        if (result != VK_SUCCESS)
        {
            throw std::runtime_error("failed to synchronize non-coherent Vulkan memory");
        }
    }
}
