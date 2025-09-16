#include "vulkan_image_memory_pool.h"
#include "log/logger.h"
namespace kpengine::graphics
{

    uint32_t VulkanImageMemoryPool::RequestMemoryTypeIndex(VkMemoryPropertyFlags memory_prop_flags, const VkMemoryRequirements &memory_require, const VkPhysicalDeviceMemoryProperties& physcial_memory_props)
    {
        for (uint32_t i = 0; i < physcial_memory_props.memoryTypeCount; i++)
        {
            if ((memory_require.memoryTypeBits & (1 << i)) && (physcial_memory_props.memoryTypes[i].propertyFlags & memory_prop_flags) == memory_prop_flags)
            {
                return i;
            }
        }

        throw std::runtime_error("Failed to find suitable memory type index");
    }

    VulkanMemoryAllocation VulkanImageMemoryPool::AllocateImageMemory(VkPhysicalDevice physical_device, VkDevice logical_device, VkImage image)
    {
        VkPhysicalDeviceMemoryProperties properties{};
        vkGetPhysicalDeviceMemoryProperties(physical_device, &properties);

        VkMemoryRequirements memory_requires{};
        vkGetImageMemoryRequirements(logical_device, image, &memory_requires);
    

        uint32_t memory_type_index = UINT32_MAX;
        try{
            memory_type_index = RequestMemoryTypeIndex(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, memory_requires, properties);
        }catch(std::exception e)
        {
            KP_LOG("VulkanImageMemoryPool", LOG_LEVEL_ERROR, "Failed to find suitable memory type index");
            throw e;
        }

        VulkanMemoryAllocation allocation = allocator_->Allocate(logical_device, memory_requires.size, memory_requires.alignment, memory_type_index);
        vkBindImageMemory(logical_device, image, allocation.memory, allocation.offset);
    }
    void VulkanImageMemoryPool::Free(VkDevice logical_device, const VulkanMemoryAllocation &allocation)
    {
        allocator_->Free(logical_device, allocation);
    }
    void VulkanImageMemoryPool::Destroy(VkDevice logical_device)
    {
        allocator_->Destroy(logical_device);
    }
}