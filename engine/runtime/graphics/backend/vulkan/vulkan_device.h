#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_DEVICE_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_DEVICE_H

#include <vector>
#include <cstdint>
#include <optional>
#include <vulkan/vulkan.h>

struct GLFWwindow;

namespace kpengine::graphics
{
    struct QueueFamilyIndices
    {
        std::optional<uint32_t> graphics_family;
        std::optional<uint32_t> present_family;
        std::optional<uint32_t> transfer_family;
        bool IsComplete() const
        {
            return graphics_family.has_value() && present_family.has_value() && transfer_family.has_value();
        }

        static QueueFamilyIndices FindQueueFamilyIndices(VkPhysicalDevice physical_device, VkSurfaceKHR surface);
    };

    struct SwapchainSupportDetail
    {
        VkSurfaceCapabilitiesKHR capacities;
        std::vector<VkSurfaceFormatKHR> surface_formats;
        std::vector<VkPresentModeKHR> present_modes;

        static SwapchainSupportDetail FindSwapchainSupports(VkPhysicalDevice device, VkSurfaceKHR surface);
    };

    struct VulkanQueue
    {
        uint32_t index = UINT_MAX;
        VkQueue queue;
    };

    /**
     * The device half of the Vulkan backend. Owns the instance, debug messenger,
     * surface, physical/logical device and the three queues, plus every query
     * that picks them. The backend owns one of these and reads handles back
     * through the accessors — swapchain/frame/resource state stays in the backend.
     */
    class VulkanDevice
    {
    public:
        VulkanDevice() = default;
        ~VulkanDevice() = default;

        void Initialize(GLFWwindow *window);
        void Destroy();

        VkInstance GetInstance() const { return instance_; }
        VkDebugUtilsMessengerEXT GetDebugMessenger() const { return debug_messager_; }
        VkSurfaceKHR GetSurface() const { return surface_; }
        VkPhysicalDevice GetPhysicalDevice() const { return physical_device_; }
        VkDevice GetLogicalDevice() const { return logical_device_; }
        const VulkanQueue &GetGraphicsQueue() const { return graphics_queue_; }
        const VulkanQueue &GetPresentQueue() const { return present_queue_; }
        const VulkanQueue &GetTransferQueue() const { return transfer_queue_; }
        bool SupportsBindlessTextures() const { return bindless_textures_enabled_; }
        uint32_t GetBindlessTextureTableCapacity() const { return bindless_texture_table_capacity_; }

    private:
        void CreateInstance();
        void CreateDebugMessager();
        void CreateSurface(GLFWwindow *window);
        void CreatePhysicalDevice();
        void CreateLogicalDevice();

        std::vector<const char *> FindRequiredExtensions() const;
        bool CheckValidationLayerSupport(const std::vector<const char *> &validation_layers) const;
        bool CheckDeviceExtensionsSupport(VkPhysicalDevice device, const std::vector<const char *> &extensions) const;
        bool CheckPhysicalDeviceSuitable(VkPhysicalDevice device) const;
        bool QueryBindlessTextureSupport(VkPhysicalDevice device, uint32_t &capacity) const;

        VkInstance instance_ = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT debug_messager_ = VK_NULL_HANDLE;
        VkSurfaceKHR surface_ = VK_NULL_HANDLE;
        VkPhysicalDevice physical_device_ = VK_NULL_HANDLE;
        VkDevice logical_device_ = VK_NULL_HANDLE;

        VulkanQueue graphics_queue_;
        VulkanQueue present_queue_;
        VulkanQueue transfer_queue_;

        bool bindless_textures_enabled_ = false;
        uint32_t bindless_texture_table_capacity_ = 0;

        std::vector<const char *> validation_layers = {"VK_LAYER_KHRONOS_validation"};
        std::vector<const char *> device_extensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
        float queue_priority = 1.f;
    };
}

#endif
