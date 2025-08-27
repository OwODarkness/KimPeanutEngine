#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_ENUM_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_ENUM_H

#include <vulkan/vulkan.h>
#include "common/enum.h"
namespace kpengine
{
    inline VkFormat ToVulkanTextureFormat(TextureFormat format)
    {
        switch (format)
        {
        case TextureFormat::TEXTURE_FORMAT_RGB8_UNORM:
            return VK_FORMAT_R8G8B8_SNORM;
        case TextureFormat::TEXTURE_FORMAT_RGB8_SRGB:
            return VK_FORMAT_R8G8B8_SRGB;
        case TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM:
            return VK_FORMAT_R8G8B8A8_SNORM;
        case TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case TextureFormat::TEXTURE_FORMAT_D24S8:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        case TextureFormat::TEXTURE_FORMAT_D32:
            return VK_FORMAT_D32_SFLOAT;

        default:
            return VK_FORMAT_UNDEFINED;
        }
    }

    inline VkImageType ToVulkanTextureType(TextureType type)
    {
        switch (type)
        {
        case TextureType::TEXTURE_TYPE_1D:
            return VK_IMAGE_TYPE_1D;
        case TextureType::TEXTURE_TYPE_2D:
            return VK_IMAGE_TYPE_2D;
        case TextureType::TEXTURE_TYPE_3D:
            return VK_IMAGE_TYPE_3D;
        default:
            return VK_IMAGE_TYPE_MAX_ENUM;
        }
    }

    inline VkImageUsageFlags ToVulkanTextureUsage(TextureUsage usage)
    {
        VkImageUsageFlags flags = 0;
        if ((uint32_t)usage & (uint32_t)TextureUsage::TEXTURE_USAGE_SAMPLE)
            flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
        if ((uint32_t)usage & (uint32_t)TextureUsage::TEXTURE_USAGE_COLOR_ATTACHMENT)
            flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if ((uint32_t)usage & (uint32_t)TextureUsage::TEXTURE_USAGE_DEPTHSTENCIL_ATTACHMENT)
            flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if ((uint32_t)usage & (uint32_t)TextureUsage::TEXTURE_USAGE_STORAGE)
            flags |= VK_IMAGE_USAGE_STORAGE_BIT;
        if ((uint32_t)usage & (uint32_t)TextureUsage::TEXTURE_USAGE_TRANSFER_SRC)
            flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if ((uint32_t)usage & (uint32_t)TextureUsage::TEXTURE_USAGE_TRANSFER_DST)
            flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        return flags;
    }

    inline VkSampleCountFlagBits ToSampleCount(uint32_t sample_count)
    {
        switch (sample_count)
        {
        case 1:
            return VK_SAMPLE_COUNT_1_BIT;
        case 2:
            return VK_SAMPLE_COUNT_2_BIT;
        case 4:
            return VK_SAMPLE_COUNT_4_BIT;
        case 8:
            return VK_SAMPLE_COUNT_8_BIT;
        case 16:
            return VK_SAMPLE_COUNT_16_BIT;
        case 32:
            return VK_SAMPLE_COUNT_32_BIT;
        case 64:
            return VK_SAMPLE_COUNT_64_BIT;
        default:
            return VK_SAMPLE_COUNT_1_BIT;
        }
    }
}

#endif