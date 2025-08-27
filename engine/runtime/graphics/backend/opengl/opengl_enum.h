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


}

#endif