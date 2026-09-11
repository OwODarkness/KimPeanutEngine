#ifndef KPENGINE_RUNTIME_GRAPHICS_TYPE_H
#define KPENGINE_RUNTIME_GRAPHICS_TYPE_H

#include <cstdint>

enum class TextureFormat
{
    TEXTURE_FORMAT_UNKNOW,
    TEXTURE_FORMAT_R8_UNORM,
    TEXTURE_FORMAT_R8_SRGB,
    TEXTURE_FORMAT_RG8_UNORM,
    TEXTURE_FORMAT_RG8_SRGB,
    TEXTURE_FORMAT_RGB8_UNORM,
    TEXTURE_FORMAT_RGB8_SRGB,
    TEXTURE_FORMAT_RGBA8_UNORM,
    TEXTURE_FORMAT_RGBA8_SRGB,
    TEXTURE_FORMAT_BGRA8_UNORM,
    TEXTURE_FORMAT_RGBA16F,
    TEXTURE_FORMAT_BC4_UNORM,
    TEXTURE_FORMAT_BC5_UNORM,
    TEXTURE_FORMAT_BC3_UNORM,
    TEXTURE_FORMAT_BC3_SRGB,
    TEXTURE_FORMAT_D24S8,
    TEXTURE_FORMAT_D32
};

// Whether a format stores 8-bit RGBA in the order the common readback seam's
// CPU image expects. SRGB and UNORM differ only in how samples are
// interpreted, not in storage, so both copy out verbatim. BGRA8 does not.
inline bool IsRgba8TextureFormat(const TextureFormat format)
{
    return format == TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM ||
           format == TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
}

// Whether the format applies the sRGB transfer function on access. Backends
// must agree on this for a clear value and for a shader output to land on the
// same stored bytes.
inline bool IsSrgbTextureFormat(const TextureFormat format)
{
    switch (format)
    {
    case TextureFormat::TEXTURE_FORMAT_R8_SRGB:
    case TextureFormat::TEXTURE_FORMAT_RG8_SRGB:
    case TextureFormat::TEXTURE_FORMAT_RGB8_SRGB:
    case TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB:
    case TextureFormat::TEXTURE_FORMAT_BC3_SRGB:
        return true;
    default:
        return false;
    }
}

enum class ShaderStage : uint8_t
{
    SHADER_STAGE_UNKNOW,
    SHADER_STAGE_VERTEX,
    SHADER_STAGE_FRAGMENT,
    SHADER_STAGE_GEOMETRY,
    SHADER_STAGE_COMPUTE
};

enum class ShaderFormat
{
    Unknown,
    SHADER_FORMAT_GLSL,
    SHADER_FORMAT_HLSL
};


#endif
