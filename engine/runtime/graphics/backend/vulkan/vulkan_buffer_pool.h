#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_BUFFER_POOL_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_BUFFER_POOL_H

#include <vector>
#include <vulkan/vulkan.h>
#include "common/api.h"

namespace kpengine::graphics{

    struct VulkanBufferResource{
        VkBuffer buffer ;
        VkDeviceMemory memory ;
        uint32_t generation = 0;
        bool alive = false;
    };

    class VulkanBufferPool{
    public:
        //TODO: add memory propities
        BufferHandle CreateBufferResource(VkPhysicalDevice physical_device, VkDevice logicial_device, VkBufferCreateInfo buffer_create_info, VkMemoryPropertyFlags properties);
        bool DestroyBufferResource(VkDevice logicial_device, BufferHandle handle);
        VulkanBufferResource* GetBufferResource(BufferHandle handle);
        void BindBufferData(VkDevice logicial_device, BufferHandle handle, VkDeviceSize size, void* src);
    private:
        std::vector<VulkanBufferResource> buffer_resources_;
        std::vector<uint32_t> free_slots;
    
    };
}

#endif