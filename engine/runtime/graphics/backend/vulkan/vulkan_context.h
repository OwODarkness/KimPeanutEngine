#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_CONTEXT_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_CONTEXT_H

#include <vulkan/vulkan.h>
namespace kpengine::graphics{
    struct VulkanContext{
        VkDevice logical_device = VK_NULL_HANDLE;
        VkPhysicalDevice physical_device = VK_NULL_HANDLE;
        class VulkanBackend* backend = nullptr;
    };
}

#endif