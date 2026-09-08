#include "live2d_product.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <limits>
#include <set>

namespace kpengine::live2d
{
    namespace
    {
        constexpr std::array<unsigned char, 8> kMagic =
            {'K', 'P', 'L', '2', 'D', 'P', 'R', 'D'};
        constexpr std::uint32_t kSupportedProductVersion = 1;
        constexpr std::uint32_t kMaxCollectionCount = 4096;
        constexpr std::uint32_t kMaxFieldBytes = 1024u * 1024u;
        constexpr std::uint64_t kMaxProductBytes = 512ull * 1024ull * 1024ull;

        void AppendU32(std::vector<std::byte> &bytes, std::uint32_t value)
        {
            for (unsigned int shift = 0; shift < 32; shift += 8)
            {
                bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
            }
        }

        bool ReadU32(const std::vector<std::byte> &bytes, std::size_t &offset,
                     std::uint32_t &value)
        {
            if (offset > bytes.size() || bytes.size() - offset < sizeof(std::uint32_t))
            {
                return false;
            }
            value = 0;
            for (unsigned int shift = 0; shift < 32; shift += 8)
            {
                value |= static_cast<std::uint32_t>(
                             std::to_integer<unsigned char>(bytes[offset++]))
                         << shift;
            }
            return true;
        }

        bool AppendBlob(std::vector<std::byte> &bytes,
                        const std::vector<std::byte> &blob,
                        std::string &diagnostic)
        {
            if (blob.size() > std::numeric_limits<std::uint32_t>::max())
            {
                diagnostic = "Live2D product field exceeds 32-bit size";
                return false;
            }
            AppendU32(bytes, static_cast<std::uint32_t>(blob.size()));
            bytes.insert(bytes.end(), blob.begin(), blob.end());
            return true;
        }

        bool AppendString(std::vector<std::byte> &bytes, const std::string &value,
                          std::string &diagnostic)
        {
            if (value.empty() || value.size() > kMaxFieldBytes ||
                value.find('\0') != std::string::npos)
            {
                diagnostic = "Live2D product contains an invalid string field";
                return false;
            }
            std::vector<std::byte> encoded;
            encoded.reserve(value.size());
            for (const char character : value)
            {
                encoded.push_back(static_cast<std::byte>(character));
            }
            return AppendBlob(bytes, encoded, diagnostic);
        }

        bool ReadBlob(const std::vector<std::byte> &bytes, std::size_t &offset,
                      std::vector<std::byte> &blob, std::string &diagnostic)
        {
            std::uint32_t size = 0;
            if (offset > bytes.size() || !ReadU32(bytes, offset, size) ||
                size > kMaxFieldBytes ||
                bytes.size() - offset < size)
            {
                diagnostic = "Live2D product contains a truncated or oversized field";
                return false;
            }
            blob.assign(bytes.begin() + static_cast<std::ptrdiff_t>(offset),
                        bytes.begin() + static_cast<std::ptrdiff_t>(offset + size));
            offset += size;
            return true;
        }

        bool ReadString(const std::vector<std::byte> &bytes, std::size_t &offset,
                        std::string &value, std::string &diagnostic)
        {
            std::vector<std::byte> encoded;
            if (!ReadBlob(bytes, offset, encoded, diagnostic) || encoded.empty())
            {
                if (diagnostic.empty())
                {
                    diagnostic = "Live2D product contains an empty string field";
                }
                return false;
            }
            value.clear();
            value.reserve(encoded.size());
            for (const std::byte character : encoded)
            {
                const unsigned char value_byte = std::to_integer<unsigned char>(character);
                if (value_byte == 0)
                {
                    diagnostic = "Live2D product string contains a NUL byte";
                    return false;
                }
                value.push_back(static_cast<char>(value_byte));
            }
            return true;
        }

        bool ValidateResource(const Live2DProductData &resource,
                              std::string &diagnostic)
        {
            if (resource.product_version != kSupportedProductVersion ||
                resource.model3_version == 0 || resource.moc_bytes.empty() ||
                resource.moc_bytes.size() > kMaxFieldBytes ||
                resource.textures.empty() || resource.textures.size() > kMaxCollectionCount ||
                resource.optional_chunks.size() > kMaxCollectionCount)
            {
                diagnostic = "Live2D product has an unsupported version or missing required data";
                return false;
            }
            std::set<std::string> texture_paths;
            for (const Live2DTextureDependency &texture : resource.textures)
            {
                if (texture.path.empty() || texture.path.size() > kMaxFieldBytes ||
                    texture.path.find('\0') != std::string::npos ||
                    !texture_paths.insert(texture.path).second)
                {
                    diagnostic = "Live2D product contains an invalid texture dependency";
                    return false;
                }
            }
            for (const Live2DOptionalChunk &chunk : resource.optional_chunks)
            {
                if (chunk.name.empty() || chunk.name.size() > kMaxFieldBytes ||
                    chunk.bytes.size() > kMaxFieldBytes)
                {
                    diagnostic = "Live2D product contains an invalid optional chunk";
                    return false;
                }
            }
            return true;
        }
    }

    bool SerializeLive2DProduct(const Live2DProductData &resource,
                                std::vector<std::byte> &bytes,
                                std::string &diagnostic)
    {
        diagnostic.clear();
        bytes.clear();
        if (!ValidateResource(resource, diagnostic))
        {
            return false;
        }

        bytes.reserve(32 + resource.moc_bytes.size());
        for (const unsigned char character : kMagic)
        {
            bytes.push_back(static_cast<std::byte>(character));
        }
        AppendU32(bytes, resource.product_version);
        AppendU32(bytes, resource.model3_version);
        AppendU32(bytes, static_cast<std::uint32_t>(resource.textures.size()));
        AppendU32(bytes, static_cast<std::uint32_t>(resource.optional_chunks.size()));
        if (!AppendBlob(bytes, resource.moc_bytes, diagnostic))
        {
            return false;
        }
        for (const Live2DTextureDependency &texture : resource.textures)
        {
            if (!AppendString(bytes, texture.path, diagnostic))
            {
                return false;
            }
        }
        for (const Live2DOptionalChunk &chunk : resource.optional_chunks)
        {
            if (!AppendString(bytes, chunk.name, diagnostic) ||
                !AppendBlob(bytes, chunk.bytes, diagnostic))
            {
                return false;
            }
        }
        return true;
    }

    bool ParseLive2DProduct(const std::vector<std::byte> &bytes,
                            Live2DProductData &resource,
                            std::string &diagnostic)
    {
        diagnostic.clear();
        resource = {};
        if (bytes.size() < kMagic.size() + 16 || bytes.size() > kMaxProductBytes)
        {
            diagnostic = "Live2D product size is invalid";
            return false;
        }

        std::size_t offset = 0;
        for (const unsigned char expected : kMagic)
        {
            if (std::to_integer<unsigned char>(bytes[offset++]) != expected)
            {
                diagnostic = "Live2D product magic is invalid";
                return false;
            }
        }
        std::uint32_t texture_count = 0;
        std::uint32_t chunk_count = 0;
        if (!ReadU32(bytes, offset, resource.product_version) ||
            !ReadU32(bytes, offset, resource.model3_version) ||
            !ReadU32(bytes, offset, texture_count) ||
            !ReadU32(bytes, offset, chunk_count) ||
            texture_count == 0 || texture_count > kMaxCollectionCount ||
            chunk_count > kMaxCollectionCount ||
            !ReadBlob(bytes, offset, resource.moc_bytes, diagnostic))
        {
            if (diagnostic.empty())
            {
                diagnostic = "Live2D product header is invalid";
            }
            return false;
        }
        resource.textures.resize(texture_count);
        for (Live2DTextureDependency &texture : resource.textures)
        {
            if (!ReadString(bytes, offset, texture.path, diagnostic))
            {
                return false;
            }
        }
        resource.optional_chunks.resize(chunk_count);
        for (Live2DOptionalChunk &chunk : resource.optional_chunks)
        {
            if (!ReadString(bytes, offset, chunk.name, diagnostic) ||
                !ReadBlob(bytes, offset, chunk.bytes, diagnostic))
            {
                return false;
            }
        }
        if (offset != bytes.size() || !ValidateResource(resource, diagnostic))
        {
            if (diagnostic.empty())
            {
                diagnostic = "Live2D product has trailing or invalid data";
            }
            return false;
        }
        return true;
    }

    bool ReadLive2DProduct(const std::filesystem::path &path,
                           Live2DProductData &resource,
                           std::string &diagnostic)
    {
        diagnostic.clear();
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        if (error || size == 0 || size > kMaxProductBytes)
        {
            diagnostic = "Live2D product file is missing or too large";
            return false;
        }
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            diagnostic = "failed to open Live2D product";
            return false;
        }
        std::vector<std::byte> bytes(static_cast<std::size_t>(size));
        file.read(reinterpret_cast<char *>(bytes.data()),
                  static_cast<std::streamsize>(bytes.size()));
        if (!file)
        {
            diagnostic = "failed to read Live2D product";
            return false;
        }
        return ParseLive2DProduct(bytes, resource, diagnostic);
    }

}
