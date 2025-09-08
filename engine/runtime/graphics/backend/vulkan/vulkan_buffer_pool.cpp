#include "vulkan_buffer_pool.h"
#include "log/logger.h"
#include "vulkan_memory_dedicated_allocator.h"
#include "vulkan_memory_linear_allocator.h"
#include "vulkan_memory_pool_allocator.h"
namespace kpengine::graphics
{
    uint32_t VulkanBufferPool::RequestMemoryTypeIndex(VkMemoryPropertyFlags memory_prop_flags, const VkMemoryRequirements &memory_require, const VkPhysicalDeviceMemoryProperties physcial_memory_props)
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

    BufferHandle VulkanBufferPool::CreateBufferResource(VkPhysicalDevice physical_device, VkDevice logicial_device, const VkBufferCreateInfo *buffer_create_info, VkMemoryPropertyFlags properties)
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

        VkMemoryPropertyFlags host_vis_flags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        VkMemoryPropertyFlags device_local_flags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        uint32_t memory_type_index = UINT32_MAX;
        try
        {
            memory_type_index = RequestMemoryTypeIndex(properties, memory_requires, memory_props);
        }
        catch (std::exception e)
        {
            KP_LOG("VulkanBufferPool", LOG_LEVEL_ERROR, "Failed to find suitable memory type");
            throw std::runtime_error("Failed to find suitable memory type!");
        }

        if (host_vis_flags == properties)
        {
            if (!host_vis_memory_allocator)
            {

                host_vis_memory_allocator = std::make_unique<VulkanMemoryPoolAllocator>();
            }
            buffer_resource.allocation = host_vis_memory_allocator->Allocate(logicial_device, memory_requires.size, memory_requires.alignment, memory_type_index);
        }

        if (device_local_flags == properties)
        {
            if (!device_local_memory_allocator)
            {
                device_local_memory_allocator = std::make_unique<VulkanMemoryPoolAllocator>();
            }

            buffer_resource.allocation = device_local_memory_allocator->Allocate(logicial_device, memory_requires.size, memory_requires.alignment, memory_type_index);
        }

        vkBindBufferMemory(logicial_device, buffer_resource.buffer, buffer_resource.allocation.memory, buffer_resource.allocation.offset);

        buffer_resource.mem_prop_flags = properties;
        buffer_resource.alive = true;
        return {id, buffer_resource.generation};
    }

    bool VulkanBufferPool::DestroyBufferResource(VkDevice logicial_device, BufferHandle handle)
    {
        if (handle.id >= buffer_resources_.size())
        {
            KP_LOG("VulkanBufferPoolLog", LOG_LEVEL_WARNNING, "Failed to destory buffer resource,  out of range");
            return false;
        }

        VulkanBufferResource &buffer_resource = buffer_resources_[handle.id];
        vkDestroyBuffer(logicial_device, buffer_resource.buffer, nullptr);
        if (!buffer_resource.allocation.owner)
        {
            KP_LOG("VulkanBufferPoolLog", LOG_LEVEL_WARNNING, "missing owner of MemoryAllocation, could cause memory leak");
        }
        else
        {

            buffer_resource.allocation.owner->Free(logicial_device, buffer_resource.allocation);
        }

        buffer_resource.alive = false;
        buffer_resource.generation++;
        return true;
    }

    VulkanBufferResource *VulkanBufferPool::GetBufferResource(BufferHandle handle)
    {
        if (handle.id >= buffer_resources_.size())
        {
            KP_LOG("VulkanBufferPool", LOG_LEVEL_ERROR, "Failed to get buffer resource, out of range");
            return nullptr;
        }

        VulkanBufferResource &buffer_resource = buffer_resources_[handle.id];
        if (!handle.IsValid() || handle.generation != buffer_resource.generation || !buffer_resource.alive)
        {
            KP_LOG("VulkanBufferPool", LOG_LEVEL_ERROR, "Failed to get buffer resource, generation mismatch or not alive");
            return nullptr;
        }

        return &buffer_resource;
    }

    void VulkanBufferPool::BindBufferData(VkDevice logicial_device, BufferHandle handle, VkDeviceSize size, const void *src)
    {
        VulkanBufferResource *buffer_resource = GetBufferResource(handle);
        if ((buffer_resource->mem_prop_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0)
        {
            KP_LOG("VulkanBufferPool", LOG_LEVEL_WARNNING, "Try to bindbuffer data by mapmemory, but memory prop flags don't hold VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT");
            return;
        }
        void *target;
        vkMapMemory(logicial_device, buffer_resource->allocation.memory, buffer_resource->allocation.offset, size, 0, &target);
        memcpy(target, src, static_cast<size_t>(size));
        vkUnmapMemory(logicial_device, buffer_resource->allocation.memory);
    }

    void VulkanBufferPool::FreeMemory(VkDevice logicial_device)
    {
        if (host_vis_memory_allocator)
            host_vis_memory_allocator->Destroy(logicial_device);
        if (device_local_memory_allocator)
            device_local_memory_allocator->Destroy(logicial_device);
    }

}