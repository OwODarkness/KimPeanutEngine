#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_BUFFER_POOL_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_BUFFER_POOL_H

#include <vector>
#include <memory>
#include <vulkan/vulkan.h>
#include "common/api.h"
#include "vulkan_memory_allocator.h"

namespace kpengine::graphics
{
    struct VulkanBufferResource
    {
        VkBuffer buffer;
        MemoryAllocation allocation;
        uint32_t generation = 0;
        VkMemoryPropertyFlags mem_prop_flags;
        bool alive = false;
    };

    class VulkanBufferPool
    {

    public:
        BufferHandle CreateBufferResource(VkPhysicalDevice physical_device, VkDevice logicial_device, const VkBufferCreateInfo *buffer_create_info, VkMemoryPropertyFlags properties);
        bool DestroyBufferResource(VkDevice logicial_device, BufferHandle handle);
        VulkanBufferResource *GetBufferResource(BufferHandle handle);
        void BindBufferData(VkDevice logicial_device, BufferHandle handle, VkDeviceSize size, const void *src);
        void FreeMemory(VkDevice logicial_device);

    private:
        uint32_t RequestMemoryTypeIndex(VkMemoryPropertyFlags memory_prop_flags, const VkMemoryRequirements& memory_require, const VkPhysicalDeviceMemoryProperties);

    private:
        std::vector<VulkanBufferResource> buffer_resources_;
        std::vector<uint32_t> free_slots;

        std::unique_ptr<IMemoryAllocator> host_vis_memory_allocator;
        std::unique_ptr<IMemoryAllocator> device_local_memory_allocator;
    };
}


#endif