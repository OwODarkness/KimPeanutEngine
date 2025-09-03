#ifndef KPENGINE_RUNTIME_GRAPHICS_VULKAN_ENUM_H
#define KPENGINE_RUNTIME_GRAPHICS_VULKAN_ENUM_H

#include <vulkan/vulkan.h>
#include "common/enum.h"
namespace kpengine::graphics
{
    inline VkFormat ConvertToVulkanTextureFormat(TextureFormat format)
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

    inline VkImageType ConvertToVulkanTextureType(TextureType type)
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

    inline VkImageUsageFlags ConvertToVulkanTextureUsage(TextureUsage usage)
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

    inline VkSampleCountFlagBits ConvertToVulkanSampleCount(uint32_t sample_count)
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

    inline VkFormat ConvertToVulkanVertexFormat(VertexFormat format)
    {
        switch (format)
        {
        case VertexFormat::VERTEX_FORMAT_ONE_INT:
            return VK_FORMAT_R32_SINT;
        case VertexFormat::VERTEX_FORMAT_TWO_INTS:
            return VK_FORMAT_R32G32_SINT;
        case VertexFormat::VERTEX_FORMAT_THREE_INTS:
            return VK_FORMAT_R32G32B32_SINT;
        case VertexFormat::VERTEX_FORMAT_FOUR_INTS:
            return VK_FORMAT_R32G32B32A32_SINT;
        case VertexFormat::VERTEX_FORMAT_ONE_FLOAT:
            return VK_FORMAT_R32_SFLOAT;
        case VertexFormat::VERTEX_FORMAT_TWO_FLOATS:
            return VK_FORMAT_R32G32_SFLOAT;
        case VertexFormat::VERTEX_FORMAT_THREE_FLOATS:
            return VK_FORMAT_R32G32B32_SFLOAT;
        case VertexFormat::VERTEX_FORMAT_FOUR_FLOATS:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        default:
            return VK_FORMAT_R32G32B32_SFLOAT;
        }
    }

    inline VkPrimitiveTopology ConvertToVulkanPrimitiveTopology(PrimitiveTopologyType topology_type)
    {
        switch (topology_type)
        {
        case PrimitiveTopologyType::PRIMITIVE_TOPOLOGY_POINT_LIST:
            return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
        case PrimitiveTopologyType::PRIMITIVE_TOPOLOGY_LINE_LIST:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        case PrimitiveTopologyType::PRIMITIVE_TOPOLOGY_LINE_STRIP:
            return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        case PrimitiveTopologyType::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        case PrimitiveTopologyType::PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
        case PrimitiveTopologyType::PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
        case PrimitiveTopologyType::PRIMITIVE_TOPOLOGY_LINE_LIST_ADJACENCY:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;
        case PrimitiveTopologyType::PRIMITIVE_TOPOLOGY_LINE_STRIP_ADJACENCY:
            return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;
        case PrimitiveTopologyType::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_ADJACENCY:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;
        case PrimitiveTopologyType::PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_ADJACENCY:
            return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;
        case PrimitiveTopologyType::PRIMITIVE_TOPOLOGY_PATCH_LIST:
            return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;
        default:
            return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
        }
    }
}

#endif