#include "native_texture.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <utility>

#include "data/texture_mipmap.h"

namespace kpengine::asset
{
    namespace
    {
        constexpr std::array<std::uint8_t, 8> kMagic{{'K', 'P', 'T', 'E', 'X', 'T', '1', '\0'}};

        std::size_t BytesPerPixel(TextureFormat format)
        {
            switch (format)
            {
            case TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM:
            case TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB:
                return 4;
            case TextureFormat::TEXTURE_FORMAT_RGBA16F:
                return 8;
            default:
                return 0;
            }
        }

        bool IsKnownSemantic(data::TextureSemantic semantic) noexcept
        {
            switch (semantic)
            {
            case data::TextureSemantic::Generic:
            case data::TextureSemantic::Color:
            case data::TextureSemantic::Normal:
            case data::TextureSemantic::PackedLinear:
            case data::TextureSemantic::OpacityMask:
                return true;
            }
            return false;
        }

        bool CheckedAdd(std::size_t lhs, std::size_t rhs, std::size_t &result)
        {
            if (rhs > std::numeric_limits<std::size_t>::max() - lhs)
            {
                return false;
            }
            result = lhs + rhs;
            return true;
        }

        bool CheckedMultiply(std::size_t lhs, std::size_t rhs, std::size_t &result)
        {
            if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs)
            {
                return false;
            }
            result = lhs * rhs;
            return true;
        }

        [[noreturn]] void Fail(NativeTextureErrorCode code, const char *message)
        {
            throw NativeTextureError(code, message);
        }

        void AppendByte(std::vector<std::byte> &bytes, std::uint8_t value)
        {
            bytes.push_back(static_cast<std::byte>(value));
        }

        void AppendU16(std::vector<std::byte> &bytes, std::uint16_t value)
        {
            AppendByte(bytes, static_cast<std::uint8_t>(value));
            AppendByte(bytes, static_cast<std::uint8_t>(value >> 8));
        }

        void AppendU32(std::vector<std::byte> &bytes, std::uint32_t value)
        {
            for (std::size_t shift = 0; shift < 32; shift += 8)
            {
                AppendByte(bytes, static_cast<std::uint8_t>(value >> shift));
            }
        }

        void AppendU64(std::vector<std::byte> &bytes, std::uint64_t value)
        {
            for (std::size_t shift = 0; shift < 64; shift += 8)
            {
                AppendByte(bytes, static_cast<std::uint8_t>(value >> shift));
            }
        }

        void AppendPayload(std::vector<std::byte> &bytes, const std::vector<std::uint8_t> &payload)
        {
            for (const std::uint8_t value : payload)
            {
                AppendByte(bytes, value);
            }
        }

        void CopyPayload(const std::vector<std::byte> &bytes, std::size_t offset,
                         std::vector<std::uint8_t> &payload)
        {
            if (offset > bytes.size() || payload.size() > bytes.size() - offset)
            {
                Fail(NativeTextureErrorCode::InvalidDirectory, "native texture payload range is invalid");
            }
            for (std::size_t index = 0; index < payload.size(); ++index)
            {
                payload[index] = std::to_integer<std::uint8_t>(bytes[offset + index]);
            }
        }

        void WriteAtHash(std::vector<std::byte> &bytes, std::size_t offset,
                         const ContentHash &hash)
        {
            if (offset > bytes.size() || bytes.size() - offset < hash.bytes.size())
            {
                Fail(NativeTextureErrorCode::InvalidArgument, "native texture digest offset is invalid");
            }
            for (std::size_t index = 0; index < hash.bytes.size(); ++index)
            {
                bytes[offset + index] = static_cast<std::byte>(hash.bytes[index]);
            }
        }

        std::uint16_t ReadU16(const std::vector<std::byte> &bytes, std::size_t offset)
        {
            return static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
                   static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 8;
        }

        std::uint32_t ReadU32(const std::vector<std::byte> &bytes, std::size_t offset)
        {
            std::uint32_t value = 0;
            for (std::size_t shift = 0; shift < 32; shift += 8)
            {
                value |= static_cast<std::uint32_t>(
                             std::to_integer<std::uint8_t>(bytes[offset + shift / 8]))
                         << shift;
            }
            return value;
        }

        std::uint64_t ReadU64(const std::vector<std::byte> &bytes, std::size_t offset)
        {
            std::uint64_t value = 0;
            for (std::size_t shift = 0; shift < 64; shift += 8)
            {
                value |= static_cast<std::uint64_t>(
                             std::to_integer<std::uint8_t>(bytes[offset + shift / 8]))
                         << shift;
            }
            return value;
        }

        ContentHash ReadHash(const std::vector<std::byte> &bytes, std::size_t offset)
        {
            ContentHash result{};
            for (std::size_t index = 0; index < result.bytes.size(); ++index)
            {
                result.bytes[index] = std::to_integer<std::uint8_t>(bytes[offset + index]);
            }
            return result;
        }

        std::size_t ExpectedLevelBytes(std::uint32_t width, std::uint32_t height,
                                       TextureFormat format)
        {
            std::size_t pixels = 0;
            std::size_t bytes = 0;
            if (!CheckedMultiply(width, height, pixels) ||
                !CheckedMultiply(pixels, BytesPerPixel(format), bytes))
            {
                Fail(NativeTextureErrorCode::Overflow, "native texture level size overflows");
            }
            return bytes;
        }

        void ValidateData(const data::TextureData &data)
        {
            const std::size_t bytes_per_pixel = BytesPerPixel(data.format);
            if (data.width == 0 || data.height == 0 || bytes_per_pixel == 0 ||
                data.depth != 1 || data.array_layers != 1 || !IsKnownSemantic(data.semantic) ||
                data.width > kNativeTextureMaxDimension || data.height > kNativeTextureMaxDimension)
            {
                Fail(NativeTextureErrorCode::InvalidValue, "native texture metadata is invalid");
            }
            if (!data::IsTextureMipChainValid(data))
            {
                Fail(NativeTextureErrorCode::InvalidValue, "native texture mip chain is invalid");
            }
            if (data.GetMipLevelCount() > kNativeTextureMaxMipLevels)
            {
                Fail(NativeTextureErrorCode::Overflow, "native texture mip count exceeds the limit");
            }
            std::size_t total = 0;
            if (!CheckedAdd(total, data.pixels.size(), total))
            {
                Fail(NativeTextureErrorCode::Overflow, "native texture size overflows");
            }
            for (const data::TextureMipSubresource &level : data.mip_subresources)
            {
                if (!CheckedAdd(total, level.pixels.size(), total))
                {
                    Fail(NativeTextureErrorCode::Overflow, "native texture size overflows");
                }
            }
            if (total > kNativeTextureMaxBytes)
            {
                Fail(NativeTextureErrorCode::Overflow, "native texture exceeds the product size limit");
            }
        }
    }

    NativeTextureError::NativeTextureError(NativeTextureErrorCode code, std::string message)
        : std::runtime_error(std::move(message)), code_(code)
    {
    }

    NativeTextureErrorCode NativeTextureError::Code() const noexcept
    {
        return code_;
    }

    std::vector<std::byte> SerializeNativeTexture(const data::TextureData &data)
    {
        ValidateData(data);
        const std::uint32_t mip_count = data.GetMipLevelCount();
        const std::size_t directory_end = kNativeTextureHeaderSize +
                                           kNativeTextureMipEntrySize * mip_count;
        std::size_t next_offset = directory_end;
        const auto append_level = [&next_offset](const std::vector<std::uint8_t> &level)
        {
            const std::size_t current = next_offset;
            if (!CheckedAdd(next_offset, level.size(), next_offset))
            {
                Fail(NativeTextureErrorCode::Overflow, "native texture size overflows");
            }
            return current;
        };

        struct Entry
        {
            std::uint32_t width{};
            std::uint32_t height{};
            std::size_t offset{};
            std::size_t size{};
        };
        std::vector<Entry> entries;
        entries.reserve(mip_count);
        entries.push_back({data.width, data.height, append_level(data.pixels), data.pixels.size()});
        for (const data::TextureMipSubresource &level : data.mip_subresources)
        {
            entries.push_back({level.width, level.height, append_level(level.pixels), level.pixels.size()});
        }
        if (next_offset > kNativeTextureMaxBytes || next_offset > std::numeric_limits<std::uint32_t>::max())
        {
            Fail(NativeTextureErrorCode::Overflow, "native texture product is too large");
        }

        std::vector<std::byte> bytes;
        bytes.reserve(next_offset);
        for (const std::uint8_t value : kMagic) AppendByte(bytes, value);
        AppendU16(bytes, kNativeTextureVersion);
        AppendU16(bytes, static_cast<std::uint16_t>(kNativeTextureHeaderSize));
        AppendU32(bytes, kNativeTextureFeatures);
        AppendU64(bytes, static_cast<std::uint64_t>(next_offset));
        AppendU64(bytes, kNativeTextureHeaderSize);
        AppendU32(bytes, mip_count);
        AppendU32(bytes, static_cast<std::uint32_t>(data.format));
        AppendU32(bytes, static_cast<std::uint32_t>(data.semantic));
        AppendU32(bytes, data.width);
        AppendU32(bytes, data.height);
        AppendU32(bytes, data.depth);
        AppendU32(bytes, data.array_layers);
        for (std::size_t index = 0; index < kNativeTextureDigestSize; ++index) AppendByte(bytes, 0);
        if (bytes.size() != kNativeTextureHeaderSize)
        {
            Fail(NativeTextureErrorCode::InvalidArgument, "native texture header size is inconsistent");
        }
        for (std::size_t index = 0; index < entries.size(); ++index)
        {
            const Entry &entry = entries[index];
            AppendU32(bytes, entry.width);
            AppendU32(bytes, entry.height);
            AppendU32(bytes, 0);
            AppendU64(bytes, static_cast<std::uint64_t>(entry.offset));
            AppendU64(bytes, static_cast<std::uint64_t>(entry.size));
            AppendU32(bytes, static_cast<std::uint32_t>(index));
        }
        AppendPayload(bytes, data.pixels);
        for (std::size_t index = 1; index < entries.size(); ++index)
        {
            AppendPayload(bytes, data.mip_subresources[index - 1].pixels);
        }
        if (bytes.size() != next_offset)
        {
            Fail(NativeTextureErrorCode::InvalidArgument, "native texture payload layout is inconsistent");
        }
        const auto hashes = Sha256WithZeroedRange(bytes, kNativeTextureDigestOffset,
                                                   kNativeTextureDigestSize);
        if (!hashes)
        {
            Fail(NativeTextureErrorCode::Truncated, "native texture digest is truncated");
        }
        const ContentHash digest = hashes->zeroed_range_hash;
        WriteAtHash(bytes, kNativeTextureDigestOffset, digest);
        return bytes;
    }

    NativeTextureProduct DeserializeNativeTexture(const std::vector<std::byte> &bytes,
                                                  const ContentHashPair *verified_hashes)
    {
        if (bytes.size() > kNativeTextureMaxBytes || bytes.size() < kNativeTextureHeaderSize)
        {
            Fail(NativeTextureErrorCode::Truncated, "native texture size is invalid");
        }
        for (std::size_t index = 0; index < kMagic.size(); ++index)
        {
            if (std::to_integer<std::uint8_t>(bytes[index]) != kMagic[index])
            {
                Fail(NativeTextureErrorCode::InvalidArgument, "native texture magic is invalid");
            }
        }
        const std::uint16_t version = ReadU16(bytes, 8);
        const std::uint16_t header_size = ReadU16(bytes, 10);
        const std::uint32_t features = ReadU32(bytes, 12);
        const std::uint64_t total_size = ReadU64(bytes, 16);
        const std::uint64_t directory_offset = ReadU64(bytes, 24);
        const std::uint32_t mip_count = ReadU32(bytes, 32);
        const TextureFormat format = static_cast<TextureFormat>(ReadU32(bytes, 36));
        const data::TextureSemantic semantic =
            static_cast<data::TextureSemantic>(ReadU32(bytes, 40));
        const std::uint32_t width = ReadU32(bytes, 44);
        const std::uint32_t height = ReadU32(bytes, 48);
        const std::uint32_t depth = ReadU32(bytes, 52);
        const std::uint32_t array_layers = ReadU32(bytes, 56);

        if (version != kNativeTextureVersion) Fail(NativeTextureErrorCode::UnsupportedVersion, "native texture version is unsupported");
        if (header_size != kNativeTextureHeaderSize || directory_offset != kNativeTextureHeaderSize)
            Fail(NativeTextureErrorCode::InvalidDirectory, "native texture header or directory is invalid");
        if (features != kNativeTextureFeatures) Fail(NativeTextureErrorCode::UnsupportedFeatures, "native texture features are unsupported");
        if (total_size != bytes.size() || mip_count == 0 || mip_count > kNativeTextureMaxMipLevels)
            Fail(NativeTextureErrorCode::InvalidDirectory, "native texture total size or mip count is invalid");
        const std::size_t directory_size = static_cast<std::size_t>(mip_count) * kNativeTextureMipEntrySize;
        std::size_t directory_end = 0;
        if (!CheckedAdd(kNativeTextureHeaderSize, directory_size, directory_end) || directory_end > bytes.size())
            Fail(NativeTextureErrorCode::Truncated, "native texture directory is truncated");
        ContentHashPair hashes{};
        if (verified_hashes != nullptr)
        {
            hashes = *verified_hashes;
        }
        else
        {
            const auto computed = Sha256WithZeroedRange(bytes, kNativeTextureDigestOffset,
                                                        kNativeTextureDigestSize);
            if (!computed)
            {
                Fail(NativeTextureErrorCode::Truncated, "native texture digest is truncated");
            }
            hashes = *computed;
        }
        const ContentHash stored_digest = ReadHash(bytes, kNativeTextureDigestOffset);
        if (stored_digest != hashes.zeroed_range_hash)
            Fail(NativeTextureErrorCode::IntegrityMismatch, "native texture integrity digest does not match");

        data::TextureData data{};
        data.width = width;
        data.height = height;
        data.depth = depth;
        data.array_layers = array_layers;
        data.format = format;
        data.semantic = semantic;
        std::size_t expected_offset = directory_end;
        for (std::uint32_t index = 0; index < mip_count; ++index)
        {
            const std::size_t offset = kNativeTextureHeaderSize +
                                       static_cast<std::size_t>(index) * kNativeTextureMipEntrySize;
            const std::uint32_t level_width = ReadU32(bytes, offset);
            const std::uint32_t level_height = ReadU32(bytes, offset + 4);
            const std::uint32_t reserved = ReadU32(bytes, offset + 8);
            const std::uint64_t payload_offset = ReadU64(bytes, offset + 12);
            const std::uint64_t payload_size = ReadU64(bytes, offset + 20);
            const std::uint32_t level_index = ReadU32(bytes, offset + 28);
            if (reserved != 0 || level_index != index || payload_offset != expected_offset ||
                payload_offset > bytes.size() || payload_size > bytes.size() - payload_offset ||
                payload_size != ExpectedLevelBytes(level_width, level_height, format))
                Fail(NativeTextureErrorCode::InvalidDirectory, "native texture mip directory is invalid");
            if (!CheckedAdd(expected_offset, static_cast<std::size_t>(payload_size), expected_offset))
                Fail(NativeTextureErrorCode::Overflow, "native texture mip payload overflows");
            if (index == 0)
            {
                data.pixels.resize(static_cast<std::size_t>(payload_size));
                CopyPayload(bytes, static_cast<std::size_t>(payload_offset), data.pixels);
            }
            else
            {
                data.mip_subresources.push_back({level_width, level_height,
                    std::vector<std::uint8_t>(static_cast<std::size_t>(payload_size))});
                CopyPayload(bytes, static_cast<std::size_t>(payload_offset),
                            data.mip_subresources.back().pixels);
            }
        }
        if (expected_offset != bytes.size())
            Fail(NativeTextureErrorCode::InvalidDirectory, "native texture has trailing payload bytes");
        ValidateData(data);
        return {std::move(data), stored_digest, hashes.content_hash};
    }

    ContentHash ComputeNativeTextureProductHash(const std::vector<std::byte> &bytes)
    {
        return Sha256(bytes);
    }
}
