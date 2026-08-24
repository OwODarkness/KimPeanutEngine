#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_MEMORY_POOL_ALLOCATOR_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_MEMORY_POOL_ALLOCATOR_H

#include <memory>
#include <vector>

#include "vulkan_memory_allocator.h"
#include "vulkan_memory_free_range_list.h"

namespace kpengine::graphics
{
    class VulkanMemoryPoolAllocator final : public IVulkanMemoryAllocator
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
            VulkanMemoryFreeRangeList free_ranges;
        };

        uint32_t CreateBlock(VkDevice logical_device,
                             const VulkanMemoryAllocationRequest &request,
                             VkDeviceSize block_size);
        VulkanMemoryAllocation AllocateFromBlock(uint32_t block_index,
                                                 const VulkanMemoryAllocationRequest &request);
        void ReleaseBlock(VkDevice logical_device, MemoryBlock &block) noexcept;

        static constexpr VkDeviceSize kDefaultBlockSize = 64ull * 1024ull * 1024ull;
        std::vector<std::unique_ptr<MemoryBlock>> blocks_;
    };
}

#endif
