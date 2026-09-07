#ifndef KPENGINE_RUNTIME_TEXTURE_MIPMAP_H
#define KPENGINE_RUNTIME_TEXTURE_MIPMAP_H

#include <cstdint>
#include <string_view>

#include "data/texture.h"

namespace kpengine::data
{
    struct TextureMipGenerationSettings
    {
        // Loose runtime textures must not produce an unbounded full-resolution
        // chain. Native products can replace this fallback with a profile.
        uint32_t max_dimension = 2048;
        // Zero means generate through the 1x1 level.
        uint32_t max_levels = 0;
    };

    // Transitional loose textures do not carry native semantic metadata yet.
    TextureSemantic ClassifyTextureSemantic(std::string_view path);

    bool IsTextureMipChainValid(const TextureData &texture) noexcept;

    // Builds a complete initialized chain. Existing valid subresources are
    // preserved; malformed partial chains are rejected.
    bool GenerateTextureMipChain(
        TextureData &texture,
        TextureSemantic semantic,
        const TextureMipGenerationSettings &settings = {});
}

#endif
