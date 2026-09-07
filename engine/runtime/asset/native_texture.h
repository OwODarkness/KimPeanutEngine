#ifndef KPENGINE_RUNTIME_ASSET_NATIVE_TEXTURE_H
#define KPENGINE_RUNTIME_ASSET_NATIVE_TEXTURE_H

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "asset_product.h"
#include "data/texture.h"

namespace kpengine::asset
{
    constexpr std::uint16_t kNativeTextureVersion = 1;
    constexpr std::uint32_t kNativeTextureFeatures = 0;
    constexpr std::size_t kNativeTextureHeaderSize = 92;
    constexpr std::size_t kNativeTextureMipEntrySize = 32;
    constexpr std::size_t kNativeTextureDigestOffset = 60;
    constexpr std::size_t kNativeTextureDigestSize = kModelArchiveHashSize;
    constexpr std::size_t kNativeTextureMaxBytes = 512u * 1024u * 1024u;
    constexpr std::uint32_t kNativeTextureMaxDimension = 16384;
    constexpr std::uint32_t kNativeTextureMaxMipLevels = 32;

    enum class NativeTextureErrorCode : std::uint8_t
    {
        InvalidArgument,
        Truncated,
        UnsupportedVersion,
        UnsupportedFeatures,
        Overflow,
        InvalidDirectory,
        InvalidValue,
        IntegrityMismatch,
    };

    class NativeTextureError final : public std::runtime_error
    {
    public:
        NativeTextureError(NativeTextureErrorCode code, std::string message);

        NativeTextureErrorCode Code() const noexcept;

    private:
        NativeTextureErrorCode code_{};
    };

    struct NativeTextureProduct
    {
        data::TextureData data;
        ContentHash integrity_digest{};
        ContentHash product_hash{};
    };

    // The product is a canonical little-endian container. It contains level
    // zero and every explicit mip payload, so runtime loading never decodes a
    // source image or invokes a backend mip-generation path.
    std::vector<std::byte> SerializeNativeTexture(const data::TextureData &data);

    // Validates the directory and digest before allocating any mip payload.
    NativeTextureProduct DeserializeNativeTexture(const std::vector<std::byte> &bytes);

    ContentHash ComputeNativeTextureProductHash(const std::vector<std::byte> &bytes);
}

#endif
