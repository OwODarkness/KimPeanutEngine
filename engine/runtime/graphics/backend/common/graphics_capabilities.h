#ifndef KPENGINE_RUNTIME_GRAPHICS_GRAPHICS_CAPABILITIES_H
#define KPENGINE_RUNTIME_GRAPHICS_GRAPHICS_CAPABILITIES_H

#include <cstdint>

#include "bindless_texture.h"
#include "base/graphics_type.h"

namespace kpengine::graphics
{
    // Effective capabilities of the initialized common RHI path. A feature is
    // true only when the backend enables it and the common contract exposes it.
    struct GraphicsCapabilities
    {
        uint32_t max_sampled_textures_per_shader_stage = 0;
        bool bindless_textures = false;
        // Zero when the common bindless path is unavailable. A supporting
        // backend reports the usable table capacity, clamped to the common
        // ABI maximum in BindlessTextureTableLayout.
        uint32_t bindless_texture_table_capacity = 0;
        bool bc4_unorm_textures = false;
        bool bc5_unorm_textures = false;
        bool bc3_unorm_textures = false;
        bool bc3_srgb_textures = false;

        constexpr bool SupportsBindlessTextures() const noexcept
        {
            return bindless_textures &&
                   IsBindlessTextureTableCapacityValid(bindless_texture_table_capacity);
        }

        constexpr bool SupportsTextureFormat(TextureFormat format) const noexcept
        {
            switch (format)
            {
            case TextureFormat::TEXTURE_FORMAT_BC4_UNORM:
                return bc4_unorm_textures;
            case TextureFormat::TEXTURE_FORMAT_BC5_UNORM:
                return bc5_unorm_textures;
            case TextureFormat::TEXTURE_FORMAT_BC3_UNORM:
                return bc3_unorm_textures;
            case TextureFormat::TEXTURE_FORMAT_BC3_SRGB:
                return bc3_srgb_textures;
            default:
                return true;
            }
        }
    };
}

#endif
