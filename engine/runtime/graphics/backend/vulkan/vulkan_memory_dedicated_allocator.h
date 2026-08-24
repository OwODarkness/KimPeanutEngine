#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_MEMORY_DEDICATED_ALLOCATOR_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_MEMORY_DEDICATED_ALLOCATOR_H

#include <memory>
#include <vector>

#include "vulkan_memory_allocator.h"

namespace kpengine::graphics
{
    class VulkanMemoryDedicatedAllocator final : public IVulkanMemoryAllocator
    {
    public:
        VulkanMemoryAllocation Allocate(
            VkDevice logical_device, const VulkanMemoryAllocationRequest &request) override;
        void Free(VkDevice logical_device, VulkanMemoryAllocation &allocation) noexcept override;
        void Destroy(VkDevice logical_device) noexcept override;

    private:
        struct MemoryBlock
        {
            VkDeviceMemory memory = VK_NULL_HANDLE;
            std::byte *mapped_base = nullptr;
            VkDeviceSize size = 0;
            uint32_t memory_type_index = UINT32_MAX;
            VkMemoryPropertyFlags properties = 0;
        };

        void ReleaseBlock(VkDevice logical_device, MemoryBlock &block) noexcept;
        std::vector<std::unique_ptr<MemoryBlock>> blocks_;
    };
}

#endif
