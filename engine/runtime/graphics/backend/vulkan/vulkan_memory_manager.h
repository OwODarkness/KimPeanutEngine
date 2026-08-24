#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_MEMORY_MANAGER_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_MEMORY_MANAGER_H

#include <cstdint>
#include <memory>

#include <vulkan/vulkan.h>

#include "vulkan_memory_allocator.h"

namespace kpengine::graphics
{
    enum class VulkanMemoryAllocationPolicy
    {
        SharedBlock,
        Dedicated,
    };

    // Vulkan-private policy and synchronization facade. Concrete allocators own
    // native blocks; this manager chooses one and hides that decision from RHI users.
    class VulkanMemoryManager
    {
    public:
        VulkanMemoryManager(VkPhysicalDevice physical_device, VkDevice logical_device);
        ~VulkanMemoryManager();

        VulkanMemoryManager(const VulkanMemoryManager &) = delete;
        VulkanMemoryManager &operator=(const VulkanMemoryManager &) = delete;

        VulkanMemoryAllocation Allocate(
            const VkMemoryRequirements &requirements,
            VkMemoryPropertyFlags required_properties,
            VulkanMemoryAllocationPolicy policy,
            const VkMemoryDedicatedAllocateInfo *dedicated_info = nullptr);
        void Free(VulkanMemoryAllocation &allocation) noexcept;

        void Write(const VulkanMemoryAllocation &allocation, const void *source,
                   VkDeviceSize size, VkDeviceSize offset = 0);
        void Flush(const VulkanMemoryAllocation &allocation, VkDeviceSize size,
                   VkDeviceSize offset = 0);
        void Invalidate(const VulkanMemoryAllocation &allocation, VkDeviceSize size,
                        VkDeviceSize offset = 0);

        void Destroy() noexcept;

    private:
        static VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment);
        uint32_t FindMemoryType(uint32_t memory_type_bits,
                                VkMemoryPropertyFlags required_properties) const;
        void FlushOrInvalidate(const VulkanMemoryAllocation &allocation,
                               VkDeviceSize size, VkDeviceSize offset, bool flush);

        VkDevice logical_device_ = VK_NULL_HANDLE;
        VkPhysicalDeviceMemoryProperties memory_properties_{};
        VkDeviceSize non_coherent_atom_size_ = 1;
        VkDeviceSize dedicated_threshold_ = 4ull * 1024ull * 1024ull;
        std::unique_ptr<IVulkanMemoryAllocator> shared_allocator_;
        std::unique_ptr<IVulkanMemoryAllocator> dedicated_allocator_;
        bool destroyed_ = false;
    };
}

#endif
