#include "vulkan_swapchain.h"

#include <algorithm>
#include <array>
#include <limits>

#include <GLFW/glfw3.h>

#include "log/logger.h"

namespace kpengine::graphics
{
#define KP_VULKAN_SWAPCHAIN_LOG_NAME "VulkanSwapchainLog"

    void VulkanSwapchain::Initialize(VulkanDevice *device, GLFWwindow *window)
    {
        device_ = device;
        window_ = window;
        CreateSwapchain(0, 0);
        CreateSwapchainImageViews();
    }

    void VulkanSwapchain::Recreate(uint32_t fallback_width, uint32_t fallback_height)
    {
        vkDeviceWaitIdle(device_->GetLogicalDevice());
        Cleanup();
        CreateSwapchain(fallback_width, fallback_height);
        CreateSwapchainImageViews();
    }

    void VulkanSwapchain::Cleanup()
    {
        for (size_t i = 0; i < swapchain_imageviews_.size(); i++)
        {
            vkDestroyImageView(device_->GetLogicalDevice(), swapchain_imageviews_[i], nullptr);
        }
        vkDestroySwapchainKHR(device_->GetLogicalDevice(), swapchain_, nullptr);
    }

    void VulkanSwapchain::CreateSwapchain(uint32_t fallback_width, uint32_t fallback_height)
    {
        VkSwapchainCreateInfoKHR swapchain_create_info{};
        swapchain_create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        swapchain_create_info.surface = device_->GetSurface();
        swapchain_create_info.clipped = VK_TRUE;
        swapchain_create_info.imageArrayLayers = 1;
        swapchain_create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        SwapchainSupportDetail swapchain_support_detail = SwapchainSupportDetail::FindSwapchainSupports(device_->GetPhysicalDevice(), device_->GetSurface());
        VkExtent2D resolution = ChooseSwapChainExtent(swapchain_support_detail.capacities, fallback_width, fallback_height);
        VkSurfaceFormatKHR surface_format = ChooseSwapChainSurfaceFormat(swapchain_support_detail.surface_formats);
        VkPresentModeKHR present_mode = ChooseSwapChainPresentMode(swapchain_support_detail.present_modes);
        uint32_t min_image_count = swapchain_support_detail.capacities.minImageCount + 1;
        if (swapchain_support_detail.capacities.maxImageCount > 0 && min_image_count > swapchain_support_detail.capacities.maxImageCount)
        {
            min_image_count = swapchain_support_detail.capacities.maxImageCount;
        }

        swapchain_create_info.imageExtent = resolution;
        swapchain_create_info.minImageCount = min_image_count;
        swapchain_create_info.imageFormat = surface_format.format;
        swapchain_create_info.imageColorSpace = surface_format.colorSpace;
        swapchain_create_info.presentMode = present_mode;

        std::array<uint32_t, 2> queue_family_raw_indices = {device_->GetGraphicsQueue().index, device_->GetPresentQueue().index};
        if (device_->GetGraphicsQueue().index != device_->GetPresentQueue().index)
        {
            swapchain_create_info.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            swapchain_create_info.queueFamilyIndexCount = static_cast<uint32_t>(queue_family_raw_indices.size());
            swapchain_create_info.pQueueFamilyIndices = queue_family_raw_indices.data();
        }
        else
        {
            swapchain_create_info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            swapchain_create_info.queueFamilyIndexCount = 0;
            swapchain_create_info.pQueueFamilyIndices = nullptr;
        }
        swapchain_create_info.oldSwapchain = VK_NULL_HANDLE;
        swapchain_create_info.preTransform = swapchain_support_detail.capacities.currentTransform;
        swapchain_create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

        if (vkCreateSwapchainKHR(device_->GetLogicalDevice(), &swapchain_create_info, nullptr, &swapchain_) != VK_SUCCESS)
        {
            KP_LOG(KP_VULKAN_SWAPCHAIN_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create swapchain");
            throw std::runtime_error("Failed to find swapchain");
        }

        // get swapchain image
        extent_ = resolution;
        swapchain_image_format_ = surface_format.format;
        uint32_t swapchain_image_count = 0;
        vkGetSwapchainImagesKHR(device_->GetLogicalDevice(), swapchain_, &swapchain_image_count, nullptr);
        swapchain_images_.resize(swapchain_image_count);
        vkGetSwapchainImagesKHR(device_->GetLogicalDevice(), swapchain_, &swapchain_image_count, swapchain_images_.data());
    }

    void VulkanSwapchain::CreateSwapchainImageViews()
    {
        swapchain_imageviews_.resize(swapchain_images_.size());
        for (size_t i = 0; i < swapchain_images_.size(); i++)
        {
            VkImageViewCreateInfo create_info{};
            create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            create_info.image = swapchain_images_[i];
            create_info.format = swapchain_image_format_;
            create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;

            create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;

            create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            create_info.subresourceRange.baseMipLevel = 0;
            create_info.subresourceRange.levelCount = 1;
            create_info.subresourceRange.baseArrayLayer = 0;
            create_info.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device_->GetLogicalDevice(), &create_info, nullptr, &swapchain_imageviews_[i]) != VK_SUCCESS)
            {
                KP_LOG(KP_VULKAN_SWAPCHAIN_LOG_NAME, LOG_LEVEL_ERROR, "Failed to create image view");
                throw std::runtime_error("Failed to create image view");
            }
        }
    }

    VkSurfaceFormatKHR VulkanSwapchain::ChooseSwapChainSurfaceFormat(const std::vector<VkSurfaceFormatKHR> &available_formats) const
    {
        // ImGui colors are authored for a non-sRGB presentation attachment. Keep
        // gamma conversion scoped to the offscreen scene render target instead of
        // applying it to every editor widget through an sRGB swapchain write.
        for (const auto &avail_format : available_formats)
        {
            if ((avail_format.format == VK_FORMAT_R8G8B8A8_UNORM ||
                 avail_format.format == VK_FORMAT_B8G8R8A8_UNORM) &&
                avail_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return avail_format;
            }
        }

        // Fall back to the usual sRGB format only when the surface exposes no
        // compatible UNORM presentation format.
        return available_formats[0];
    }

    VkPresentModeKHR VulkanSwapchain::ChooseSwapChainPresentMode(const std::vector<VkPresentModeKHR> &available_modes) const
    {
        for (const auto &avail_mode : available_modes)
        {
            if (avail_mode == VK_PRESENT_MODE_MAILBOX_KHR)
            {
                return VK_PRESENT_MODE_MAILBOX_KHR;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    VkExtent2D VulkanSwapchain::ChooseSwapChainExtent(const VkSurfaceCapabilitiesKHR &capacity, uint32_t fallback_width, uint32_t fallback_height) const
    {
        if (capacity.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            return capacity.currentExtent;
        }
        else
        {
            int height{};
            int width{};
            if (has_resized_)
            {
                width = fallback_width;
                height = fallback_height;
            }
            else
            {
                glfwGetWindowSize(window_, &width, &height);
            }
            VkExtent2D actual_size{static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
            actual_size.width = std::clamp(actual_size.width, capacity.minImageExtent.width, capacity.maxImageExtent.width);
            actual_size.height = std::clamp(actual_size.height, capacity.minImageExtent.height, capacity.maxImageExtent.height);
            return actual_size;
        }
    }

    uint32_t VulkanSwapchain::GetMaxUsableSampleCount() const
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device_->GetPhysicalDevice(), &properties);

        VkSampleCountFlags sample_count = properties.limits.framebufferColorSampleCounts & properties.limits.framebufferDepthSampleCounts;
        if (sample_count & VK_SAMPLE_COUNT_64_BIT)
        {
            return 64;
        }
        if (sample_count & VK_SAMPLE_COUNT_32_BIT)
        {
            return 32;
        }
        if (sample_count & VK_SAMPLE_COUNT_16_BIT)
        {
            return 16;
        }
        if (sample_count & VK_SAMPLE_COUNT_8_BIT)
        {
            return 8;
        }
        if (sample_count & VK_SAMPLE_COUNT_4_BIT)
        {
            return 4;
        }
        if (sample_count & VK_SAMPLE_COUNT_2_BIT)
        {
            return 2;
        }
        return 1;
    }
}
