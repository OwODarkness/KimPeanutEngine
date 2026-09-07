#ifndef KPENGINE_RUNTIME_TEXTURE_DATA_H
#define KPENGINE_RUNTIME_TEXTURE_DATA_H

#include <cstddef>
#include <cstdint>
#include <vector>

#include "base/graphics_type.h"

namespace kpengine::data
{
    enum class TextureSemantic : uint8_t
    {
        Generic,
        Color,
        Normal,
        PackedLinear,
        OpacityMask,
    };

    struct TextureMipSubresource
    {
        uint32_t width = 0;
        uint32_t height = 0;
        std::vector<uint8_t> pixels;
    };

    struct TextureData
    {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t depth = 1;
        uint32_t array_layers = 1;
        TextureFormat format = TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
        TextureSemantic semantic = TextureSemantic::Generic;
        // Level zero remains in pixels for compatibility with existing CPU
        // consumers. mip_subresources contains levels one through N in order.
        std::vector<uint8_t> pixels;
        std::vector<TextureMipSubresource> mip_subresources;

        uint32_t GetMipLevelCount() const noexcept
        {
            return 1U + static_cast<uint32_t>(mip_subresources.size());
        }

        size_t GetTotalByteCount() const noexcept
        {
            size_t total = pixels.size();
            for (const TextureMipSubresource &level : mip_subresources)
            {
                total += level.pixels.size();
            }
            return total;
        }
    };
}

#endif
