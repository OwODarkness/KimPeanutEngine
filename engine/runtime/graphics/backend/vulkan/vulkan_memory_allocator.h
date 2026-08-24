#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_MEMORY_ALLOCATOR_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_MEMORY_ALLOCATOR_H

#include <cstddef>
#include <cstdint>

#include <vulkan/vulkan.h>

namespace kpengine::graphics
{
    enum class VulkanMemoryAllocationKind : uint8_t
    {
        SharedBlock,
        Dedicated,
    };

    class IVulkanMemoryAllocator;

    // Opaque allocation metadata shared by buffer and image owners. Native memory
    // remains owned by `owner`; callers only bind the returned range and release it.
    // `mapped_address`, when present, already includes `offset` and stays valid
    // until Free; no caller may map or unmap `memory` itself.
    struct VulkanMemoryAllocation
    {
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkDeviceSize offset = 0;
        VkDeviceSize size = 0;
        VkDeviceSize backing_size = 0;
        std::byte *mapped_address = nullptr;
        uint32_t memory_type_index = UINT32_MAX;
        VkMemoryPropertyFlags properties = 0;
        uint32_t block_index = UINT32_MAX;
        VulkanMemoryAllocationKind kind = VulkanMemoryAllocationKind::SharedBlock;
        IVulkanMemoryAllocator *owner = nullptr;

        bool IsValid() const { return memory != VK_NULL_HANDLE && owner != nullptr; }
    };

    struct VulkanMemoryAllocationRequest
    {
        VkDeviceSize size = 0;
        VkDeviceSize alignment = 1;
        uint32_t memory_type_index = UINT32_MAX;
        VkMemoryPropertyFlags properties = 0;
        // Consumed synchronously by vkAllocateMemory (for example, dedicated-info).
        const void *allocation_pnext = nullptr;
    };

    class IVulkanMemoryAllocator
    {
    public:
        virtual ~IVulkanMemoryAllocator() = default;

        virtual VulkanMemoryAllocation Allocate(
            VkDevice logical_device, const VulkanMemoryAllocationRequest &request) = 0;
        virtual void Free(VkDevice logical_device, VulkanMemoryAllocation &allocation) noexcept = 0;
        virtual void Destroy(VkDevice logical_device) noexcept = 0;
    };
}

#endif
