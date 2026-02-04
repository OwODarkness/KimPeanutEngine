#include "vulkan_buffer_manager.h"
#include "log/logger.h"
#include "vulkan_memory_dedicated_allocator.h"
#include "vulkan_memory_linear_allocator.h"
#include "vulkan_memory_pool_allocator.h"
namespace kpengine::graphics
{
    uint32_t VulkanBufferManager::RequestMemoryTypeIndex(VkMemoryPropertyFlags memory_prop_flags, const VkMemoryRequirements &memory_require, const VkPhysicalDeviceMemoryProperties physcial_memory_props)
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

    BufferHandle VulkanBufferManager::CreateBufferResource(VkPhysicalDevice physical_device, VkDevice logicial_device, const VkBufferCreateInfo *buffer_create_info, VulkanMemoryUsageType memory_type)
    {
        BufferHandle handle = handle_system_.Create();
        if(handle.id == buffer_resources_.size())
        {
            buffer_resources_.emplace_back();
        }

        VulkanBufferResource &buffer_resource = buffer_resources_[handle.id];

        if (vkCreateBuffer(logicial_device, buffer_create_info, nullptr, &buffer_resource.buffer) != VK_SUCCESS)
        {
            KP_LOG("VulkanBufferManager", LOG_LEVEL_ERROR, "Failed to create buffer");
            throw std::runtime_error("Failed to create buffer");
        }

        VkMemoryRequirements memory_requires;
        vkGetBufferMemoryRequirements(logicial_device, buffer_resource.buffer, &memory_requires);

        VkPhysicalDeviceMemoryProperties memory_props;
        vkGetPhysicalDeviceMemoryProperties(physical_device, &memory_props);

        VkMemoryPropertyFlags properties;
        if (memory_type == VulkanMemoryUsageType::MEMORY_USAGE_DEVICE)
        {
            properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        }
        else if (memory_type == VulkanMemoryUsageType::MEMORY_USAGE_STAGING || memory_type == VulkanMemoryUsageType::MEMORY_USAGE_UNIFORM)
        {
            properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        }
        uint32_t memory_type_index = UINT32_MAX;
        try
        {
            memory_type_index = RequestMemoryTypeIndex(properties, memory_requires, memory_props);
        }
        catch (std::exception e)
        {
            KP_LOG("VulkanBufferManager", LOG_LEVEL_ERROR, "Failed to find suitable memory type");
            throw std::runtime_error("Failed to find suitable memory type!");
        }

        IVulkanMemoryAllocator* allocator = nullptr;
        if(buffer_create_info->size > pool_max_size)
        {
            if (dedicated_allocators_.find(memory_type) == dedicated_allocators_.end() || dedicated_allocators_[memory_type] == nullptr)
            {
                dedicated_allocators_[memory_type] = std::make_unique<VulkanMemoryDedicatedAllocator>();
            }
            allocator = dedicated_allocators_[memory_type].get();
        }
        else
        {
            if (memory_allocators_.find(memory_type) == memory_allocators_.end() || memory_allocators_[memory_type] == nullptr)
            {
                memory_allocators_[memory_type] = std::make_unique<VulkanMemoryPoolAllocator>();
            }
            allocator = memory_allocators_[memory_type].get();
        }

        buffer_resource.allocation = allocator->Allocate(logicial_device, memory_requires.size, memory_requires.alignment, memory_type_index);

        vkBindBufferMemory(logicial_device, buffer_resource.buffer, buffer_resource.allocation.memory, buffer_resource.allocation.offset);

        buffer_resource.mem_prop_flags = properties;
        buffer_resource.alive = true;
        return handle;
    }

    bool VulkanBufferManager::DestroyBufferResource(VkDevice logicial_device, BufferHandle handle)
    {
        VulkanBufferResource* buffer_resource = GetBufferResource(handle);

        if(!buffer_resource)
        {
            return false;
        }

        vkDestroyBuffer(logicial_device, buffer_resource->buffer, nullptr);
        if (!buffer_resource->allocation.owner)
        {
            KP_LOG("VulkanBufferManager", LOG_LEVEL_WARNNING, "missing owner of MemoryAllocation, could cause memory leak");
        }
        else
        {

            buffer_resource->allocation.owner->Free(logicial_device, buffer_resource->allocation);
        }

        buffer_resource->alive = false;
        return handle_system_.Destroy(handle);
    }

    VulkanBufferResource* VulkanBufferManager::GetBufferResource(BufferHandle handle)
    {
        uint32_t index = handle_system_.Get(handle);
        if (index >= buffer_resources_.size())
        {
            KP_LOG("VulkanBufferManager", LOG_LEVEL_ERROR, "Failed to get buffer resource, out of range");
            return nullptr;
        }

        return &buffer_resources_[handle.id];
    }

    void VulkanBufferManager::UploadData(VkDevice logicial_device, BufferHandle handle, VkDeviceSize size, const void *src)
    {
        VulkanBufferResource *buffer_resource = GetBufferResource(handle);
        if ((buffer_resource->mem_prop_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0)
        {
            KP_LOG("VulkanBufferManager", LOG_LEVEL_WARNNING, "Try to bindbuffer data by mapmemory, but memory prop flags don't hold VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT");
            return;
        }
        void *target;
        vkMapMemory(logicial_device, buffer_resource->allocation.memory, buffer_resource->allocation.offset, size, 0, &target);
        memcpy(target, src, static_cast<size_t>(size));
        vkUnmapMemory(logicial_device, buffer_resource->allocation.memory);
    }

    void VulkanBufferManager::MapBuffer(VkDevice logical_device, BufferHandle handle, VkDeviceSize size, void **mapped_ptr)
    {
        VulkanBufferResource *buffer_resource = GetBufferResource(handle);
        if ((buffer_resource->mem_prop_flags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) == 0)
        {
            KP_LOG("VulkanBufferManager", LOG_LEVEL_WARNNING, "Try to bindbuffer data by mapmemory, but memory prop flags don't hold VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT");
            return;
        }

        vkMapMemory(logical_device, buffer_resource->allocation.memory, buffer_resource->allocation.offset, size, 0, mapped_ptr);
    }

    void VulkanBufferManager::FreeMemory(VkDevice logicial_device)
    {
        for(auto& allocator: memory_allocators_)
        {
            allocator.second->Destroy(logicial_device);
        }
    }

}