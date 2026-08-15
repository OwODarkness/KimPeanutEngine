#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_SWAPCHAIN_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_SWAPCHAIN_H

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

#include "vulkan_device.h"

struct GLFWwindow;

namespace kpengine::graphics
{
    /**
     * The swapchain half of the Vulkan backend. Owns the swapchain, its image
     * views, the chosen extent/format and the resize flag. The backend owns one of
     * these and delegates creation/recreation/cleanup; the resize path also
     * recreates the backend's depth/color attachments, which live outside here.
     */
    class VulkanSwapchain
    {
    public:
        VulkanSwapchain() = default;
        ~VulkanSwapchain() = default;

        void Initialize(VulkanDevice *device, GLFWwindow *window);
        void Recreate(uint32_t fallback_width, uint32_t fallback_height);
        void Cleanup();

        void MarkResized() { has_resized_ = true; }
        void ClearResized() { has_resized_ = false; }
        bool HasResized() const { return has_resized_; }

        VkSwapchainKHR GetSwapchain() const { return swapchain_; }
        VkImage GetImage(size_t index) const { return swapchain_images_[index]; }
        VkImageView GetImageView(size_t index) const { return swapchain_imageviews_[index]; }
        size_t GetImageCount() const { return swapchain_images_.size(); }
        const VkExtent2D &GetExtent() const { return extent_; }
        VkFormat GetImageFormat() const { return swapchain_image_format_; }

        uint32_t GetMaxUsableSampleCount() const;

    private:
        void CreateSwapchain(uint32_t fallback_width, uint32_t fallback_height);
        void CreateSwapchainImageViews();

        VkSurfaceFormatKHR ChooseSwapChainSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &available_formats) const;
        VkPresentModeKHR ChooseSwapChainPresentMode(const std::vector<VkPresentModeKHR> &available_modes) const;
        VkExtent2D ChooseSwapChainExtent(const VkSurfaceCapabilitiesKHR &capacity, uint32_t fallback_width, uint32_t fallback_height) const;

        VulkanDevice *device_ = nullptr;
        GLFWwindow *window_ = nullptr;

        VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
        VkExtent2D extent_{};
        VkFormat swapchain_image_format_ = VK_FORMAT_UNDEFINED;
        std::vector<VkImage> swapchain_images_;
        std::vector<VkImageView> swapchain_imageviews_;
        bool has_resized_ = false;
    };
}

#endif
