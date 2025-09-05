#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_MEMORY_LINEAR_ALLOCATOR_H
#define kPENGINE_RUTNIME_GRAPHICS_VULKAN_MEMORY_LINEAR_ALLOCATOR_H

#include <memory>
#include "vulkan_memory_allocator.h"

namespace kpengine::graphics
{

    struct MemoryBlock
    {
        VkDeviceMemory memory;
        VkDeviceSize size;
        VkDeviceSize offset;
    };

    class LinearAllocator : public IMemoryAllocator
    {
    public:
        MemoryAllocation Allocate(VkDevice logicial_device, VkDeviceSize size, VkDeviceSize alignment, uint32_t memory_type_index) override;
        void Free(VkDevice logicial_device, VkDeviceMemory memory) override;
        void Destroy(VkDevice logicial_device) override;

    private:
        void Initialize(VkDevice logicial_device, VkDeviceSize size, uint32_t memory_type_index);

    private:
        std::unique_ptr<MemoryBlock> block_;
    };
}

#endif