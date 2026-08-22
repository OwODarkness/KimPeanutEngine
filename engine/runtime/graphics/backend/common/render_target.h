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

    // Borrowed presentation data for a render target's color attachment. The
    // values are backend-native tokens only; their lifetime stays with the
    // render target and callers must never destroy or retain them past resize.
    struct RenderTargetView
    {
        uint32_t width = 0;
        uint32_t height = 0;
        uintptr_t native_image = 0;
        uintptr_t native_image_view = 0;

        bool IsValid() const
        {
            return width != 0 && height != 0 && native_image_view != 0;
        }
    };
}

#endif
