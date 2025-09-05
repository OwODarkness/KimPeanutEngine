#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_MEMORY_ALLOCATOR_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_MEMORY_ALLOCATOR_H


#include <vulkan/vulkan.h>


namespace kpengine::graphics{
   
    struct MemoryAllocation
    {
        VkDeviceMemory memory;
        VkDeviceSize size;
        VkDeviceSize offset;
        class IMemoryAllocator* owner = nullptr;
    };
        class IMemoryAllocator
    {
    public:
        virtual MemoryAllocation Allocate(VkDevice logicial_device, VkDeviceSize size, VkDeviceSize alignment, uint32_t memory_type_index) = 0;
        virtual void Free(VkDevice logicial_device, VkDeviceMemory memory) = 0;
        virtual void Destroy(VkDevice logicial_device) = 0;
    };
}

#endif