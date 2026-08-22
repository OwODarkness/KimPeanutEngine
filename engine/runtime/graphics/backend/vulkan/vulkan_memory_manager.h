#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_MEMORY_MANAGER_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_MEMORY_MANAGER_H

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include <vulkan/vulkan.h>

namespace kpengine::graphics
{
    enum class VulkanMemoryAllocationPolicy
    {
        SharedBlock,
        Dedicated,
    };

    // Buffer-facing allocation metadata. The manager owns the native memory;
    // this record only identifies a live range within that owner.
    struct VulkanBufferMemoryAllocation
    {
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        VkDeviceSize size = 0;
        std::byte *mapped_address = nullptr;
        uint32_t block_index = UINT32_MAX;
        bool dedicated = false;

        bool IsValid() const { return memory != VK_NULL_HANDLE; }
    };

    class VulkanMemoryManager
    {
    public:
        VulkanMemoryManager(VkPhysicalDevice physical_device, VkDevice logical_device);
        ~VulkanMemoryManager();

        VulkanMemoryManager(const VulkanMemoryManager &) = delete;
        VulkanMemoryManager &operator=(const VulkanMemoryManager &) = delete;

        VulkanBufferMemoryAllocation Allocate(
            const VkMemoryRequirements &requirements,
            VkMemoryPropertyFlags required_properties,
            VulkanMemoryAllocationPolicy policy,
            const VkMemoryDedicatedAllocateInfo *dedicated_info = nullptr);
        void Free(VulkanBufferMemoryAllocation &allocation) noexcept;

        void Write(const VulkanBufferMemoryAllocation &allocation, const void *source,
                   VkDeviceSize size, VkDeviceSize offset = 0);
        void Flush(const VulkanBufferMemoryAllocation &allocation, VkDeviceSize size,
                   VkDeviceSize offset = 0);
        void Invalidate(const VulkanBufferMemoryAllocation &allocation, VkDeviceSize size,
                        VkDeviceSize offset = 0);

        void Destroy() noexcept;

    private:
        struct FreeRange
        {
            VkDeviceSize offset = 0;
            VkDeviceSize size = 0;
        };

        struct MemoryBlock
        {
            VkDeviceMemory memory = VK_NULL_HANDLE;
            std::byte *mapped_base = nullptr;
            VkDeviceSize size = 0;
            uint32_t memory_type_index = UINT32_MAX;
            VkMemoryPropertyFlags properties = 0;
            std::vector<FreeRange> free_ranges;
        };

        static VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment);
        uint32_t FindMemoryType(uint32_t memory_type_bits,
                                VkMemoryPropertyFlags required_properties) const;
        uint32_t CreateBlock(VkDeviceSize size, uint32_t memory_type_index,
                             VkMemoryPropertyFlags properties, bool dedicated,
                             const VkMemoryDedicatedAllocateInfo *dedicated_info);
        VulkanBufferMemoryAllocation AllocateFromBlock(
            uint32_t block_index, const VkMemoryRequirements &requirements,
            bool dedicated);
        MemoryBlock *FindBlock(const VulkanBufferMemoryAllocation &allocation) noexcept;
        const MemoryBlock *FindBlock(const VulkanBufferMemoryAllocation &allocation) const noexcept;
        void ReleaseBlock(MemoryBlock &block) noexcept;
        void MergeFreeRanges(MemoryBlock &block);
        void FlushOrInvalidate(const VulkanBufferMemoryAllocation &allocation,
                               VkDeviceSize size, VkDeviceSize offset, bool flush);

        VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
        VkDevice logical_device_ = VK_NULL_HANDLE;
        VkPhysicalDeviceMemoryProperties memory_properties_{};
        VkDeviceSize non_coherent_atom_size_ = 1;
        VkDeviceSize shared_block_size_ = 64ull * 1024ull * 1024ull;
        VkDeviceSize dedicated_threshold_ = 4ull * 1024ull * 1024ull;
        std::vector<std::unique_ptr<MemoryBlock>> shared_blocks_;
        std::vector<std::unique_ptr<MemoryBlock>> dedicated_blocks_;
        bool destroyed_ = false;
    };
}

#endif
