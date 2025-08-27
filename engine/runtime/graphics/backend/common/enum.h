#ifndef KPENGINE_RUNTIME_GRAPHICS_BACKEND_ENUM_H
#define KPENGINE_RUNTIME_GRAPHICS_BACKEND_ENUM_H

#include <cstdint>
namespace kpengine
{
    enum class TextureFormat
    {
        TEXTURE_FORMAT_RGB8_UNORM,
        TEXTURE_FORMAT_RGB8_SRGB,
        TEXTURE_FORMAT_RGBA8_UNORM,
        TEXTURE_FORMAT_RGBA8_SRGB,
        TEXTURE_FORMAT_D24S8,
        TEXTURE_FORMAT_D32
    };

    enum class TextureType
    {
        TEXTURE_TYPE_1D,
        TEXTURE_TYPE_2D,
        TEXTURE_TYPE_3D
    };

    enum class TextureUsage : uint32_t
    {
        None = 0,
        TEXTURE_USAGE_SAMPLE = 1 << 0,
        TEXTURE_USAGE_COLOR_ATTACHMENT = 1 << 1,
        TEXTURE_USAGE_DEPTHSTENCIL_ATTACHMENT = 1 << 2,
        TEXTURE_USAGE_STORAGE = 1 << 3,
        TEXTURE_USAGE_TRANSFER_SRC = 1 << 4,
        TEXTURE_USAGE_TRANSFER_DST = 1 << 5,
    };

}

#endif