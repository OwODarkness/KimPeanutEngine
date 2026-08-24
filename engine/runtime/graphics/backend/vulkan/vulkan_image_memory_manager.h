#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_IMAGE_MEMORY_MANAGER_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_IMAGE_MEMORY_MANAGER_H

#include <vulkan/vulkan.h>

#include "vulkan_memory_allocator.h"

namespace kpengine::graphics
{
    class VulkanMemoryManager;

    // Image-specific requirements/binding adapter. Allocation policy and native
    // lifetime live in VulkanMemoryManager and its shared allocator family.
    class VulkanImageMemoryManager
    {
    public:
        explicit VulkanImageMemoryManager(VulkanMemoryManager &memory_manager);

        VulkanMemoryAllocation AllocateImageMemory(VkDevice logical_device, VkImage image);
        void Free(VulkanMemoryAllocation &allocation) noexcept;

    private:
        VulkanMemoryManager *memory_manager_ = nullptr;
    };
}

#endif
