#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_CONTEXT_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_CONTEXT_H

#include <vulkan/vulkan.h>
namespace kpengine::graphics{
    struct VulkanContext{
        VkInstance instance = VK_NULL_HANDLE;
        VkDevice logical_device = VK_NULL_HANDLE;
        VkPhysicalDevice physical_device = VK_NULL_HANDLE;
        class VulkanBufferManager* buffer_manager = nullptr;
        class VulkanUploadContext* upload_context = nullptr;
        class VulkanImageMemoryManager* image_memory_manager = nullptr;
        class VulkanEditorBridge* editor_bridge = nullptr;
    };
}

#endif
