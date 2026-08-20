#ifndef KPENGINE_RUNTIME_GRAPHICS_RENDER_TARGET_H
#define KPENGINE_RUNTIME_GRAPHICS_RENDER_TARGET_H

#include <cstdint>

#include "api.h"
#include "texture.h"

namespace kpengine::graphics
{
    // Offscreen scene output. The render module owns the target policy; the RHI
    // owns the texture attachments and their native representations.
    struct RenderTargetDesc
    {
        uint32_t width = 0;
        uint32_t height = 0;
        TextureFormat color_format = TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
        TextureFormat depth_format = TextureFormat::TEXTURE_FORMAT_D32;
    };

    struct RenderTargetResource
    {
        TextureHandle color;
        TextureHandle depth;
        RenderTargetDesc desc;
    };
}

#endif
