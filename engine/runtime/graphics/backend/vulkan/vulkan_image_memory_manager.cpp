#include "vulkan_image_memory_manager.h"

#include <stdexcept>

#include "vulkan_memory_manager.h"

namespace kpengine::graphics
{
    VulkanImageMemoryManager::VulkanImageMemoryManager(VulkanMemoryManager &memory_manager)
        : memory_manager_(&memory_manager)
    {
    }

    VulkanMemoryAllocation VulkanImageMemoryManager::AllocateImageMemory(
        VkDevice logical_device, VkImage image)
    {
        if (!memory_manager_ || image == VK_NULL_HANDLE)
        {
            throw std::runtime_error("cannot allocate memory for an invalid Vulkan image");
        }

        VkMemoryDedicatedRequirements dedicated_requirements{};
        dedicated_requirements.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
        VkMemoryRequirements2 requirements{};
        requirements.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
        requirements.pNext = &dedicated_requirements;
        VkImageMemoryRequirementsInfo2 requirements_info{};
        requirements_info.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2;
        requirements_info.image = image;
        vkGetImageMemoryRequirements2(logical_device, &requirements_info, &requirements);

        VkMemoryDedicatedAllocateInfo dedicated_info{};
        dedicated_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
        dedicated_info.image = image;
        // Required/preferred dedicated image memory must carry this image in the
        // allocation pNext chain; otherwise it may use a compatible shared block.
        const VulkanMemoryAllocationPolicy policy =
            dedicated_requirements.requiresDedicatedAllocation ||
                    dedicated_requirements.prefersDedicatedAllocation
                ? VulkanMemoryAllocationPolicy::Dedicated
                : VulkanMemoryAllocationPolicy::SharedBlock;
        VulkanMemoryAllocation allocation = memory_manager_->Allocate(
            requirements.memoryRequirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            policy, policy == VulkanMemoryAllocationPolicy::Dedicated
                        ? &dedicated_info : nullptr);
        if (vkBindImageMemory(logical_device, image, allocation.memory, allocation.offset) != VK_SUCCESS)
        {
            memory_manager_->Free(allocation);
            throw std::runtime_error("failed to bind Vulkan image memory");
        }
        return allocation;
    }

    void VulkanImageMemoryManager::Free(VulkanMemoryAllocation &allocation) noexcept
    {
        if (memory_manager_)
        {
            memory_manager_->Free(allocation);
        }
        else
        {
            allocation = {};
        }
    }
}
