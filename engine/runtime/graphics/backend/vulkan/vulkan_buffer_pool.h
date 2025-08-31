#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_BUFFER_POOL_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_BUFFER_POOL_H

#include <vector>
#include <vulkan/vulkan.h>
#include "common/api.h"

namespace kpengine::graphics{

    struct VulkanBufferResource{
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        uint32_t generation = 0;
        bool alive = false;
    };

    class VulkanBufferPool{
    public:
        VulkanBufferPool(VkPhysicalDevice physical_device, VkDevice logicial_device);
        BufferHandle CreateBufferResource(VkBufferCreateInfo buffer_create_info);
        bool DestroyBufferResource();
        void BindBufferData(size_t size, void* data);
        VulkanBufferResource* GetBufferResource(BufferHandle handle);
    private:
        VkPhysicalDevice physical_device_;
        VkDevice logicial_device_;
        std::vector<VulkanBufferResource> buffer_resources_;
        std::vector<uint32_t> free_slots;
    
    };
}

#endif