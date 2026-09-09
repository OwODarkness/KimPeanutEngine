#ifndef KPENGINE_RUNTIME_ASSET_TEXTURE_VARIANT_PROFILE_H
#define KPENGINE_RUNTIME_ASSET_TEXTURE_VARIANT_PROFILE_H

#include <cstdint>

namespace kpengine::asset
{
    // Runtime chooses one material texture product before Asset dependency
    // resolution. Asset remains API-neutral: Render reports whether the
    // complete block-compressed material profile is usable.
    enum class TextureVariantProfile : uint8_t
    {
        Portable,
        BlockCompressed,
    };
}

#endif
