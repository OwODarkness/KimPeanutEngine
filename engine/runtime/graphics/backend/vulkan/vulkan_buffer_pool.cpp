#include "vulkan_buffer_pool.h"
#include <iostream>
#include "log/logger.h"
namespace kpengine::graphics
{

    BufferHandle VulkanBufferPool::CreateBufferResource(VkPhysicalDevice physical_device, VkDevice logicial_device, const VkBufferCreateInfo* buffer_create_info, VkMemoryPropertyFlags properties)
    {
        uint32_t id = 0;
        if (!free_slots.empty())
        {
            id = free_slots.back();
            free_slots.pop_back();
        }
        else
        {
            id = static_cast<uint32_t>(buffer_resources_.size());
            buffer_resources_.emplace_back();
        }


        VulkanBufferResource &buffer_resource = buffer_resources_[id];

        if (vkCreateBuffer(logicial_device, buffer_create_info, nullptr, &buffer_resource.buffer) != VK_SUCCESS)
        {
            KP_LOG("VulkanBufferPool", LOG_LEVEL_ERROR, "Failed to create buffer");
            throw std::runtime_error("Failed to create buffer");
        }

        VkMemoryRequirements memory_requires;
        vkGetBufferMemoryRequirements(logicial_device, buffer_resource.buffer, &memory_requires);


        VkPhysicalDeviceMemoryProperties memory_props;
        vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_props);

        uint32_t memory_type_index = 0;
        bool is_found = false;
        for (uint32_t i = 0; i < memory_props.memoryTypeCount; i++)
        {
            if ((memory_requires.memoryTypeBits & (1 << i)) && (memory_props.memoryTypes[i].propertyFlags & properties) == properties)
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
        memory_allocate_info.allocationSize = memory_requires.size;
        memory_allocate_info.memoryTypeIndex = memory_type_index;

        if (vkAllocateMemory(logicial_device, &memory_allocate_info, nullptr, &buffer_resource.memory) != VK_SUCCESS)
        {
            KP_LOG("VulkanBufferPool", LOG_LEVEL_ERROR, "Failed to allocate memory");
            throw std::runtime_error("Failed to allocate memory");
        }
    
        vkBindBufferMemory(logicial_device, buffer_resource.buffer, buffer_resource.memory, 0);

        buffer_resource.mem_prop_flags = properties;

        buffer_resource.alive = true;
        return {id, buffer_resource.generation};
    }

    bool VulkanBufferPool::DestroyBufferResource(VkDevice logicial_device,BufferHandle handle)
    {
        if(handle.id >= buffer_resources_.size())
        {
            KP_LOG("VulkanBufferPool", LOG_LEVEL_WARNNING, "Failed to destory buffer resource,  out of range");
            return false;
        }

        VulkanBufferResource& buffer_resource = buffer_resources_[handle.id];
        vkDestroyBuffer(logicial_device, buffer_resource.buffer, nullptr);
        vkFreeMemory(logicial_device, buffer_resource.memory, nullptr);
        buffer_resource.alive = false;
        buffer_resource.generation++;
        return true;
    }

    VulkanBufferResource* VulkanBufferPool::GetBufferResource(BufferHandle handle)
    {
        if(handle.id >= buffer_resources_.size())
        {
            KP_LOG("VulkanBufferPool", LOG_LEVEL_ERROR, "Failed to get buffer resource, out of range");
            return nullptr;
        }

        VulkanBufferResource& buffer_resource = buffer_resources_[handle.id];
        if(!handle.IsValid() || handle.generation != buffer_resource.generation || !buffer_resource.alive)
        {
            KP_LOG("VulkanBufferPool", LOG_LEVEL_ERROR, "Failed to get buffer resource, generation mismatch or not alive");
            return nullptr;
        }

        return &buffer_resource;
    }

    void VulkanBufferPool::BindBufferData(VkDevice logicial_device, BufferHandle handle, VkDeviceSize size, const void* src)
    {
        VulkanBufferResource* buffer_resource = GetBufferResource(handle);
        // if(!buffer_resource->mem_prop_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        // {
        //     KP_LOG("VulkanBufferPool", LOG_LEVEL_WARNNING, "Try to bindbuffer data by mapmemory, but memory prop flags don't hold VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT");
        //     return ;
        // }
        void* target;    
        vkMapMemory(logicial_device, buffer_resource->memory, 0, size, 0, &target);
        memcpy(target, src, static_cast<size_t>(size));
        vkUnmapMemory(logicial_device, buffer_resource->memory);
    }


}