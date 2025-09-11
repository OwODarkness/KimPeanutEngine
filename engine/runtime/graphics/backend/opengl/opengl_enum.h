#ifndef KPENGINE_RUNTIME_GRAPHICS_OPENGL_ENUM_H
#define KPENGINE_RUNTIME_GRAPHICS_OPENGL_ENUM_H

#include <glad/glad.h>
#include "common/enum.h"

namespace kpengine::graphics
{
    inline GLenum ConvertToOpenglTextureFormat(TextureFormat format)
    {
        switch (format)
        {
        case TextureFormat::TEXTURE_FORMAT_RGB8_UNORM:
            return GL_RGB8;
        case TextureFormat::TEXTURE_FORMAT_RGB8_SRGB:
            return GL_SRGB8;
        case TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM:
            return GL_RGBA8;
        case TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB:
            return GL_SRGB8_ALPHA8;
        case TextureFormat::TEXTURE_FORMAT_D24S8:
            return GL_DEPTH24_STENCIL8;
        case TextureFormat::TEXTURE_FORMAT_D32:
            return GL_DEPTH_COMPONENT32;
        default:
            return 0;
        }
    }

    inline GLenum ConvertToOpenglTextureType(TextureType type)
    {
        switch (type)
        {
        case TextureType::TEXTURE_TYPE_1D:
            return GL_TEXTURE_1D;
        case TextureType::TEXTURE_TYPE_2D:
            return GL_TEXTURE_2D;
        case TextureType::TEXTURE_TYPE_3D:
            return GL_TEXTURE_3D;
        default:
            return 0;
        }
    }

    struct GLTextureUsage
    {
        bool is_color_attachment = false;
        bool is_depthstencil_attachment = false;
        bool is_sample = false;
        bool is_storage = false; // image load/store
    };

    inline GLTextureUsage ConvertToOpenglTextureUsageTo(TextureUsage usage)
    {
        GLTextureUsage result;
        if ((uint32_t)usage & (uint32_t)TextureUsage::TEXTURE_USAGE_SAMPLE)
            result.is_sample = true;

        if ((uint32_t)usage & (uint32_t)TextureUsage::TEXTURE_USAGE_COLOR_ATTACHMENT)
            result.is_color_attachment = true;

        if ((uint32_t)usage & (uint32_t)TextureUsage::TEXTURE_USAGE_DEPTHSTENCIL_ATTACHMENT)
            result.is_depthstencil_attachment = true;

        if ((uint32_t)usage & (uint32_t)TextureUsage::TEXTURE_USAGE_STORAGE)
            result.is_storage = true;

        return result;
    }

    inline GLenum ConvertToOpenglPrimitiveTopology(PrimitiveTopologyType topology_type)
    {
        switch (topology_type)
        {
        case PrimitiveTopologyType::PRIMITIVE_TOPOLOGY_POINT_LIST:
            return GL_POINTS;
        case PrimitiveTopologyType::PRIMITIVE_TOPOLOGY_LINE_LIST:
            return GL_LINES;
        case PrimitiveTopologyType::PRIMITIVE_TOPOLOGY_LINE_STRIP:
            return GL_LINE_STRIP;
        case PrimitiveTopologyType::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST:
            return GL_TRIANGLES;
        case PrimitiveTopologyType::PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
            return GL_TRIANGLE_STRIP;
        case PrimitiveTopologyType::PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
            return GL_TRIANGLE_FAN;
        case PrimitiveTopologyType::PRIMITIVE_TOPOLOGY_LINE_LIST_ADJACENCY:
            return GL_LINES_ADJACENCY;
        case PrimitiveTopologyType::PRIMITIVE_TOPOLOGY_LINE_STRIP_ADJACENCY:
            return GL_LINE_STRIP_ADJACENCY;
        case PrimitiveTopologyType::PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_ADJACENCY:
            return GL_TRIANGLES_ADJACENCY;
        case PrimitiveTopologyType::PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_ADJACENCY:
            return GL_TRIANGLE_STRIP_ADJACENCY;
        default:
            return GL_TRIANGLES;
        }
    }
    GLenum ConvertToGLBlendFactor(BlendFactor factor)
    {
        switch (factor)
        {
        case BlendFactor::BLEND_FACTOR_ZERO:
            return GL_ZERO;
        case BlendFactor::BLEND_FACTOR_ONE:
            return GL_ONE;
        case BlendFactor::BLEND_FACTOR_SRC_COLOR:
            return GL_SRC_COLOR;
        case BlendFactor::BLEND_FACTOR_ONE_MINUS_SRC_COLOR:
            return GL_ONE_MINUS_SRC_COLOR;
        case BlendFactor::BLEND_FACTOR_DST_COLOR:
            return GL_DST_COLOR;
        case BlendFactor::BLEND_FACTOR_ONE_MINUS_DST_COLOR:
            return GL_ONE_MINUS_DST_COLOR;
        case BlendFactor::BLEND_FACTOR_SRC_ALPHA:
            return GL_SRC_ALPHA;
        case BlendFactor::BLEND_FACTOR_ONE_MINUS_SRC_ALPHA:
            return GL_ONE_MINUS_SRC_ALPHA;
        case BlendFactor::BLEND_FACTOR_DST_ALPHA:
            return GL_DST_ALPHA;
        case BlendFactor::BLEND_FACTOR_ONE_MINUS_DST_ALPHA:
            return GL_ONE_MINUS_DST_ALPHA;
        case BlendFactor::BLEND_FACTOR_CONSTANT_COLOR:
            return GL_CONSTANT_COLOR;
        case BlendFactor::BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR:
            return GL_ONE_MINUS_CONSTANT_COLOR;
        case BlendFactor::BLEND_FACTOR_CONSTANT_ALPHA:
            return GL_CONSTANT_ALPHA;
        case BlendFactor::BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA:
            return GL_ONE_MINUS_CONSTANT_ALPHA;
        }
        return GL_ONE;
    }

    GLenum ConvertToGLBlendOp(BlendOp op)
    {
        switch (op)
        {
        case BlendOp::BLEND_OP_ADD:
            return GL_FUNC_ADD;
        case BlendOp::BLEND_OP_SUBTRACT:
            return GL_FUNC_SUBTRACT;
        case BlendOp::BLEND_OP_REVERSE_SUBTRACT:
            return GL_FUNC_REVERSE_SUBTRACT;
        case BlendOp::BLEND_OP_MIN:
            return GL_MIN;
        case BlendOp::BLEND_OP_MAX:
            return GL_MAX;
        }
        return GL_FUNC_ADD;
    }

}

#endif