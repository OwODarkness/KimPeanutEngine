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
        case TextureFormat::TEXTURE_FORMAT_R8_UNORM:
            return VK_FORMAT_R8_SNORM;
        case TextureFormat::TEXTURE_FORMAT_RG8_UNORM:
            return VK_FORMAT_R8G8_SNORM;
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

    inline VkCullModeFlags ConvertToVulkanCullMode(CullMode cull_mode)
    {
        switch (cull_mode)
        {
        case CullMode::CULL_MODE_FRONT:
            return VK_CULL_MODE_FRONT_BIT;
        case CullMode::CULL_MODE_BACK:
            return VK_CULL_MODE_BACK_BIT;
        case CullMode::CULL_MODE_FRONT_AND_BACK:
            return VK_CULL_MODE_FRONT_AND_BACK;
        default:
            return VK_CULL_MODE_FLAG_BITS_MAX_ENUM;
        }
    }

    inline VkFrontFace ConvertToVulkanFrontFace(FrontFace front_face)
    {
        switch (front_face)
        {
        case FrontFace::FRONT_FACE_CLOCKWISE:
            return VK_FRONT_FACE_CLOCKWISE;
        case FrontFace::FRONT_FACE_COUNTER_CLOCKWISE:
            return VK_FRONT_FACE_COUNTER_CLOCKWISE;
        default:
            return VK_FRONT_FACE_MAX_ENUM;
        }
    }

    inline VkPolygonMode ConvertToVulkanPolygonMode(PolygonMode polygon_mode)
    {
        switch (polygon_mode)
        {
        case PolygonMode::POLYGON_MODE_FILL:
            return VK_POLYGON_MODE_FILL;
        case PolygonMode::POLYGON_MODE_LINE:
            return VK_POLYGON_MODE_LINE;
        case PolygonMode::POLYGON_MODE_POINT:
            return VK_POLYGON_MODE_POINT;
        default:
            return VK_POLYGON_MODE_MAX_ENUM;
        }
    }

    inline VkBlendFactor ConvertToVulkanBlendFactor(BlendFactor factor)
    {
        switch (factor)
        {
        case BlendFactor::BLEND_FACTOR_ZERO:
            return VK_BLEND_FACTOR_ZERO;
        case BlendFactor::BLEND_FACTOR_ONE:
            return VK_BLEND_FACTOR_ONE;
        case BlendFactor::BLEND_FACTOR_SRC_COLOR:
            return VK_BLEND_FACTOR_SRC_COLOR;
        case BlendFactor::BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case BlendFactor::BLEND_FACTOR_DST_COLOR:
            return VK_BLEND_FACTOR_DST_COLOR;
        case BlendFactor::BLEND_FACTOR_ONE_MINUS_DST_COLOR:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case BlendFactor::BLEND_FACTOR_SRC_ALPHA:
            return VK_BLEND_FACTOR_SRC_ALPHA;
        case BlendFactor::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
            return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::BLEND_FACTOR_DST_ALPHA:
            return VK_BLEND_FACTOR_DST_ALPHA;
        case BlendFactor::BLEND_FACTOR_ONE_MINUS_DST_ALPHA:
            return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case BlendFactor::BLEND_FACTOR_CONSTANT_COLOR:
            return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case BlendFactor::BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:
            return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case BlendFactor::BLEND_FACTOR_CONSTANT_ALPHA:
            return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case BlendFactor::BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
            return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        }
        return VK_BLEND_FACTOR_ONE;
    }

    inline VkBlendOp ConvertToVulkanBlendOp(BlendOp op)
    {
        switch (op)
        {
        case BlendOp::BLEND_OP_ADD:
            return VK_BLEND_OP_ADD;
        case BlendOp::BLEND_OP_SUBTRACT:
            return VK_BLEND_OP_SUBTRACT;
        case BlendOp::BLEND_OP_REVERSE_SUBTRACT:
            return VK_BLEND_OP_REVERSE_SUBTRACT;
        case BlendOp::BLEND_OP_MIN:
            return VK_BLEND_OP_MIN;
        case BlendOp::BLEND_OP_MAX:
            return VK_BLEND_OP_MAX;
        }
        return VK_BLEND_OP_ADD;
    }

    inline VkDescriptorType ConvertToVulkanDescriptorType(DescriptorType type)
    {
        switch (type)
        {
        case DescriptorType::DESCRIPTOR_TYPE_UNIFORM:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        case DescriptorType::DESCRIPTOR_TYPE_UNIFORM_DYNAMIC:
            return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        case DescriptorType::DESCRIPTOR_TYPE_SAMPLER:
            return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        default:
            return VK_DESCRIPTOR_TYPE_MAX_ENUM; // fallback / invalid
        }
    }

    inline VkShaderStageFlags ConvertToVulkanShaderStageFlags(ShaderStage stage)
    {
        VkShaderStageFlags flags = 0;
        if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(ShaderStage::SHADER_STAGE_VERTEX))
            flags |= VK_SHADER_STAGE_VERTEX_BIT;
        if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(ShaderStage::SHADER_STAGE_FRAGMENT))
            flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
        if (static_cast<uint32_t>(stage) & static_cast<uint32_t>(ShaderStage::SHADER_STAGE_GEOMETRY))
            flags |= VK_SHADER_STAGE_GEOMETRY_BIT;

        return flags;
    }

}

#endif