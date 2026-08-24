#include "vulkan_buffer_manager.h"

#include <stdexcept>

#include "log/logger.h"

namespace kpengine::graphics
{
    VulkanBufferManager::VulkanBufferManager(VulkanMemoryManager &memory_manager)
        : memory_manager_(&memory_manager)
    {
    }

    BufferHandle VulkanBufferManager::CreateBufferResource(
        VkDevice logical_device, const VkBufferCreateInfo *buffer_create_info,
        VulkanMemoryUsageType memory_type)
    {
        if (!buffer_create_info || !memory_manager_)
        {
            throw std::runtime_error("Vulkan buffer manager is not initialized");
        }

        const BufferHandle handle = handle_system_.Create();
        if (handle.id == buffer_resources_.size())
        {
            buffer_resources_.emplace_back();
        }
        VulkanBufferResource &resource = buffer_resources_[handle.id];

        if (vkCreateBuffer(logical_device, buffer_create_info, nullptr, &resource.buffer) != VK_SUCCESS)
        {
            handle_system_.Destroy(handle);
            throw std::runtime_error("failed to create Vulkan buffer");
        }

        VkMemoryDedicatedRequirements dedicated_requirements{};
        dedicated_requirements.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
        VkMemoryRequirements2 requirements2{};
        requirements2.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
        requirements2.pNext = &dedicated_requirements;
        VkBufferMemoryRequirementsInfo2 requirements_info{};
        requirements_info.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2;
        requirements_info.buffer = resource.buffer;
        vkGetBufferMemoryRequirements2(logical_device, &requirements_info, &requirements2);

        VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        if (memory_type == VulkanMemoryUsageType::MEMORY_USAGE_STAGING ||
            memory_type == VulkanMemoryUsageType::MEMORY_USAGE_UNIFORM)
        {
            properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        }

        try
        {
            VkMemoryDedicatedAllocateInfo dedicated_allocate_info{};
            dedicated_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
            dedicated_allocate_info.buffer = resource.buffer;
            const VulkanMemoryAllocationPolicy policy =
                dedicated_requirements.requiresDedicatedAllocation ||
                        dedicated_requirements.prefersDedicatedAllocation
                    ? VulkanMemoryAllocationPolicy::Dedicated
                    : VulkanMemoryAllocationPolicy::SharedBlock;
            resource.allocation = memory_manager_->Allocate(
                requirements2.memoryRequirements, properties, policy,
                policy == VulkanMemoryAllocationPolicy::Dedicated
                    ? &dedicated_allocate_info : nullptr);
            if (vkBindBufferMemory(logical_device, resource.buffer,
                                   resource.allocation.memory,
                                   resource.allocation.offset) != VK_SUCCESS)
            {
                memory_manager_->Free(resource.allocation);
                throw std::runtime_error("failed to bind Vulkan buffer memory");
            }
        }
        catch (...)
        {
            vkDestroyBuffer(logical_device, resource.buffer, nullptr);
            resource = {};
            handle_system_.Destroy(handle);
            throw;
        }

        resource.mem_prop_flags = properties;
        resource.alive = true;
        return handle;
    }

    bool VulkanBufferManager::DestroyBufferResource(VkDevice logical_device, BufferHandle handle)
    {
        VulkanBufferResource *resource = GetBufferResource(handle);
        if (!resource)
        {
            return false;
        }

        // The buffer must die before its bound allocation can be returned to a
        // shared block or freed as dedicated VkDeviceMemory.
        vkDestroyBuffer(logical_device, resource->buffer, nullptr);
        memory_manager_->Free(resource->allocation);
        *resource = {};
        return handle_system_.Destroy(handle);
    }

    void VulkanBufferManager::DestroyAll(VkDevice logical_device)
    {
        for (VulkanBufferResource &resource : buffer_resources_)
        {
            if (!resource.alive)
            {
                continue;
            }
            // Preserve the same buffer-before-memory lifetime rule during bulk teardown.
            vkDestroyBuffer(logical_device, resource.buffer, nullptr);
            memory_manager_->Free(resource.allocation);
            resource = {};
        }
    }

    VulkanBufferResource *VulkanBufferManager::GetBufferResource(BufferHandle handle)
    {
        const uint32_t index = handle_system_.Get(handle);
        if (index >= buffer_resources_.size())
        {
            KP_LOG("VulkanBufferManager", LOG_LEVEL_ERROR,
                   "failed to get buffer resource: handle is out of range");
            return nullptr;
        }
        VulkanBufferResource &resource = buffer_resources_[index];
        return resource.alive ? &resource : nullptr;
    }

    void VulkanBufferManager::UploadData(BufferHandle handle, VkDeviceSize size,
                                         const void *source)
    {
        VulkanBufferResource *resource = GetBufferResource(handle);
        if (!resource)
        {
            throw std::runtime_error("cannot upload to an invalid Vulkan buffer");
        }
        memory_manager_->Write(resource->allocation, source, size);
    }

    void *VulkanBufferManager::GetMappedAddress(BufferHandle handle,
                                                VkDeviceSize size)
    {
        const uint32_t index = handle_system_.Get(handle);
        if (index >= buffer_resources_.size())
        {
            return nullptr;
        }
        const VulkanBufferResource &resource = buffer_resources_[index];
        if (!resource.alive || !resource.allocation.mapped_address ||
            size > resource.allocation.size)
        {
            return nullptr;
        }
        return resource.allocation.mapped_address;
    }
}
