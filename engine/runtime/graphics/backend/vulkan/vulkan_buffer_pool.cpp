#include "vulkan_buffer_pool.h"
#include "log/logger.h"
namespace kpengine::graphics
{
    VulkanBufferPool::VulkanBufferPool(VkPhysicalDevice physical_device, VkDevice logicial_device) : physical_device_(physical_device), logicial_device_(logicial_device)
    {
    }

    BufferHandle VulkanBufferPool::CreateBufferResource(VkBufferCreateInfo buffer_create_info)
    {
        uint32_t id = 0;
        if (!free_slots.empty())
        {
            id = free_slots.back();
            free_slots.pop_back();
        }
        else
        {
            id = static_cast<uint32_t>(free_slots.size());
            buffer_resources_.emplace_back();
        }

        VulkanBufferResource &buffer_resource = buffer_resources_[id];

        if (vkCreateBuffer(logicial_device_, &buffer_create_info, nullptr, &buffer_resource.buffer) != VK_SUCCESS)
        {
            KP_LOG("VulkanBufferPool", LOG_LEVEL_ERROR, "Failed to create buffer");
            throw std::runtime_error("Failed to create buffer");
        }

        VkMemoryRequirements memory_requires;
        vkGetBufferMemoryRequirements(logicial_device_, buffer_resource.buffer, &memory_requires);

        VkPhysicalDeviceMemoryProperties memory_props;
        vkGetPhysicalDeviceMemoryProperties(physical_device_, &memory_props);

        uint32_t memory_type_index = 0;
        bool is_found = false;
        for (uint32_t i = 0; i < memory_props.memoryTypeCount; i++)
        {
            if (memory_requires.memoryTypeBits & (i << 1) && (memory_requires.memoryTypeBits & memory_props.memoryTypes[i].propertyFlags) == memory_props.memoryTypes[i].propertyFlags)
            {
                is_found = true;
                memory_type_index = i;
                break;
            }
        }

        if (!is_found)
        {
            KP_LOG("VulkanBufferPool", LOG_LEVEL_ERROR, "Failed to find suitable memory type");
            throw std::runtime_error("Failed to find suitable memory type!");
        }

        VkMemoryAllocateInfo memory_allocate_info{};
        memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        memory_allocate_info.allocationSize = buffer_create_info.size;
        memory_allocate_info.memoryTypeIndex = memory_type_index;

        if (vkAllocateMemory(logicial_device_, &memory_allocate_info, nullptr, &buffer_resource.memory) != VK_SUCCESS)
        {
            KP_LOG("VulkanBufferPool", LOG_LEVEL_ERROR, "Failed to allocate memory");
            throw std::runtime_error("Failed to allocate memory");
        }
    
        buffer_resource.alive = true;
        return {id, buffer_resource.generation};
    }

    bool VulkanBufferPool::DestroyBufferResource()
    {
        return true;
    }

    VulkanBufferResource *VulkanBufferPool::GetBufferResource(BufferHandle handle)
    {
    }

}