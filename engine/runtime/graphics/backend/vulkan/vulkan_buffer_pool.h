#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_BUFFER_POOL_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_BUFFER_POOL_H

#include <vector>
#include <memory>
#include <vulkan/vulkan.h>
#include "common/api.h"

namespace kpengine::graphics
{

    struct MemoryAllocate
    {
        VkDeviceMemory memory;
        VkDeviceSize size;
        VkDeviceSize offset;
    };

    struct MemoryBlock
    {
        VkDeviceMemory memory;
        VkDeviceSize size;
        VkDeviceSize offset;
    };

    class MemoryAllocator
    {
    public:
        void Initialize(VkDevice logicial_device, VkDeviceSize size, uint32_t memory_type_index);
        MemoryAllocate Allocate(VkDeviceSize size, VkDeviceSize alignment);
        void Free(VkDevice logicial_device);

    private:
        MemoryBlock block_;
    };

    struct VulkanBufferResource
    {
        VkBuffer buffer;
        VkDeviceMemory memory;
        uint32_t generation = 0;
        VkMemoryPropertyFlags mem_prop_flags;
        bool alive = false;
    };

        struct VulkanBufferResourceV2
    {
        VkBuffer buffer;
        MemoryAllocate allocate;
        uint32_t generation = 0;
        VkMemoryPropertyFlags mem_prop_flags;
        bool alive = false;
    };


    class VulkanBufferPoolV2
    {
        
    public:
        BufferHandle CreateBufferResource(VkPhysicalDevice physical_device, VkDevice logicial_device, const VkBufferCreateInfo *buffer_create_info, VkMemoryPropertyFlags properties);
        bool DestroyBufferResource(VkDevice logicial_device, BufferHandle handle);
        VulkanBufferResourceV2 *GetBufferResource(BufferHandle handle);
        void BindBufferData(VkDevice logicial_device, BufferHandle handle, VkDeviceSize size, const void *src);
        void FreeMemory(VkDevice logicial_device);
    private:
        std::vector<VulkanBufferResourceV2> buffer_resources_;
        std::vector<uint32_t> free_slots;

        std::unique_ptr<MemoryAllocator> host_vis_memory_allocator;
        std::unique_ptr<MemoryAllocator> device_local_memory_allocator;
    };

    class VulkanBufferPool
    {
    public:
        // TODO: add memory propities
        BufferHandle CreateBufferResource(VkPhysicalDevice physical_device, VkDevice logicial_device, const VkBufferCreateInfo *buffer_create_info, VkMemoryPropertyFlags properties);
        bool DestroyBufferResource(VkDevice logicial_device, BufferHandle handle);
        VulkanBufferResource *GetBufferResource(BufferHandle handle);
        void BindBufferData(VkDevice logicial_device, BufferHandle handle, VkDeviceSize size, const void *src);

    private:
        std::vector<VulkanBufferResource> buffer_resources_;
        std::vector<uint32_t> free_slots;
    };
}

#endif