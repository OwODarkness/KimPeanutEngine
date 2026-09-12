#include "live2d_product.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <set>
#include <utility>

namespace kpengine::live2d
{
    namespace
    {
        constexpr std::array<unsigned char, 8> kMagic =
            {'K', 'P', 'L', '2', 'D', 'P', 'R', 'D'};
        constexpr std::uint32_t kV1ProductVersion = 1;
        constexpr std::uint32_t kV2ProductVersion = 2;
        constexpr std::uint32_t kMaxCollectionCount = 4096;
        constexpr std::uint32_t kMaxFieldBytes = 1024u * 1024u;
        constexpr std::uint64_t kMaxProductBytes = 512ull * 1024ull * 1024ull;

        void AppendU32(std::vector<std::byte> &bytes, const std::uint32_t value)
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

        void AppendU64(std::vector<std::byte> &bytes, const std::uint64_t value)
        {
            for (unsigned int shift = 0; shift < 64; shift += 8)
            {
                bytes.push_back(static_cast<std::byte>((value >> shift) & 0xffu));
            }
        }

        bool ReadU64(const std::vector<std::byte> &bytes, std::size_t &offset,
                     std::uint64_t &value)
        {
            if (offset > bytes.size() || bytes.size() - offset < sizeof(std::uint64_t))
            {
                return false;
            }
            value = 0;
            for (unsigned int shift = 0; shift < 64; shift += 8)
            {
                value |= static_cast<std::uint64_t>(
                             std::to_integer<unsigned char>(bytes[offset++]))
                         << shift;
            }
            return true;
        }

        void AppendDouble(std::vector<std::byte> &bytes, const double value)
        {
            std::uint64_t bits = 0;
            static_assert(sizeof(bits) == sizeof(value));
            std::memcpy(&bits, &value, sizeof(bits));
            AppendU64(bytes, bits);
        }

        bool ReadDouble(const std::vector<std::byte> &bytes, std::size_t &offset,
                        double &value)
        {
            std::uint64_t bits = 0;
            if (!ReadU64(bytes, offset, bits))
            {
                return false;
            }
            std::memcpy(&value, &bits, sizeof(value));
            return true;
        }

        bool AppendBlob(std::vector<std::byte> &bytes,
                        const std::vector<std::byte> &blob,
                        std::string &diagnostic)
        {
            if (blob.size() > kMaxFieldBytes)
            {
                diagnostic = "Live2D product field exceeds the 1 MiB limit";
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
                size > kMaxFieldBytes || bytes.size() - offset < size)
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

        bool ValidateFade(const bool present, const double value,
                          std::string &diagnostic)
        {
            if (present && (!std::isfinite(value) || value < 0.0))
            {
                diagnostic = "Live2D product contains an invalid motion fade";
                return false;
            }
            return true;
        }

        bool ValidateResource(const Live2DProductData &resource,
                              std::string &diagnostic)
        {
            if ((resource.product_version != kV1ProductVersion &&
                 resource.product_version != kV2ProductVersion) ||
                resource.model3_version == 0 || resource.moc_bytes.empty() ||
                resource.moc_bytes.size() > kMaxFieldBytes ||
                resource.textures.empty() || resource.textures.size() > kMaxCollectionCount ||
                resource.optional_chunks.size() > kMaxCollectionCount)
            {
                diagnostic = "Live2D product has an unsupported version or missing required data";
                return false;
            }

            if (resource.product_version == kV1ProductVersion &&
                (!resource.motions.empty() || !resource.expressions.empty() ||
                 !resource.parameter_groups.empty()))
            {
                diagnostic = "Live2D Product V1 cannot contain typed animation data";
                return false;
            }

            if (resource.motions.size() > kMaxCollectionCount ||
                resource.expressions.size() > kMaxCollectionCount ||
                resource.parameter_groups.size() > kMaxCollectionCount)
            {
                diagnostic = "Live2D product collection exceeds the limit";
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
                    chunk.name.find('\0') != std::string::npos ||
                    chunk.bytes.size() > kMaxFieldBytes)
                {
                    diagnostic = "Live2D product contains an invalid optional chunk";
                    return false;
                }
            }

            std::set<std::pair<std::string, std::uint32_t>> motion_keys;
            for (const Live2DAuthoredMotion &motion : resource.motions)
            {
                if (motion.group.empty() || motion.group.size() > kMaxFieldBytes ||
                    motion.group.find('\0') != std::string::npos ||
                    motion.motion_bytes.empty() ||
                    motion.motion_bytes.size() > kMaxFieldBytes ||
                    !motion_keys.insert({motion.group, motion.index}).second ||
                    !ValidateFade(motion.has_fade_in, motion.fade_in_time, diagnostic) ||
                    !ValidateFade(motion.has_fade_out, motion.fade_out_time, diagnostic) ||
                    (motion.has_sound && motion.sound_bytes.empty()) ||
                    motion.sound_bytes.size() > kMaxFieldBytes)
                {
                    if (diagnostic.empty())
                    {
                        diagnostic = "Live2D product contains an invalid motion";
                    }
                    return false;
                }
            }

            std::set<std::string> expression_names;
            for (const Live2DAuthoredExpression &expression : resource.expressions)
            {
                if (expression.name.empty() || expression.name.size() > kMaxFieldBytes ||
                    expression.name.find('\0') != std::string::npos ||
                    expression.expression_bytes.empty() ||
                    expression.expression_bytes.size() > kMaxFieldBytes ||
                    !expression_names.insert(expression.name).second)
                {
                    diagnostic = "Live2D product contains an invalid expression";
                    return false;
                }
            }

            std::set<std::string> group_names;
            for (const Live2DParameterGroup &group : resource.parameter_groups)
            {
                if (group.target != "Parameter" || group.name.empty() ||
                    group.name.size() > kMaxFieldBytes ||
                    group.name.find('\0') != std::string::npos ||
                    group.ids.empty() || group.ids.size() > kMaxCollectionCount ||
                    !group_names.insert(group.name).second)
                {
                    diagnostic = "Live2D product contains an invalid parameter group";
                    return false;
                }
                std::set<std::string> ids;
                for (const std::string &id : group.ids)
                {
                    if (id.empty() || id.size() > kMaxFieldBytes ||
                        id.find('\0') != std::string::npos ||
                        !ids.insert(id).second)
                    {
                        diagnostic = "Live2D product contains an invalid parameter ID";
                        return false;
                    }
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

        bytes.reserve(64 + resource.moc_bytes.size());
        for (const unsigned char character : kMagic)
        {
            bytes.push_back(static_cast<std::byte>(character));
        }
        AppendU32(bytes, resource.product_version);
        AppendU32(bytes, resource.model3_version);
        AppendU32(bytes, static_cast<std::uint32_t>(resource.textures.size()));
        AppendU32(bytes, static_cast<std::uint32_t>(resource.optional_chunks.size()));
        if (resource.product_version == kV2ProductVersion)
        {
            AppendU32(bytes, static_cast<std::uint32_t>(resource.motions.size()));
            AppendU32(bytes, static_cast<std::uint32_t>(resource.expressions.size()));
            AppendU32(bytes, static_cast<std::uint32_t>(resource.parameter_groups.size()));
        }
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
        for (const Live2DAuthoredMotion &motion : resource.motions)
        {
            if (!AppendString(bytes, motion.group, diagnostic))
            {
                return false;
            }
            AppendU32(bytes, motion.index);
            AppendU32(bytes, motion.has_fade_in ? 1u : 0u);
            if (motion.has_fade_in)
            {
                AppendDouble(bytes, motion.fade_in_time);
            }
            AppendU32(bytes, motion.has_fade_out ? 1u : 0u);
            if (motion.has_fade_out)
            {
                AppendDouble(bytes, motion.fade_out_time);
            }
            if (!AppendBlob(bytes, motion.motion_bytes, diagnostic))
            {
                return false;
            }
            AppendU32(bytes, motion.has_sound ? 1u : 0u);
            if (motion.has_sound && !AppendBlob(bytes, motion.sound_bytes, diagnostic))
            {
                return false;
            }
        }
        for (const Live2DAuthoredExpression &expression : resource.expressions)
        {
            if (!AppendString(bytes, expression.name, diagnostic) ||
                !AppendBlob(bytes, expression.expression_bytes, diagnostic))
            {
                return false;
            }
        }
        for (const Live2DParameterGroup &group : resource.parameter_groups)
        {
            if (!AppendString(bytes, group.target, diagnostic) ||
                !AppendString(bytes, group.name, diagnostic) ||
                group.ids.size() > kMaxCollectionCount)
            {
                return false;
            }
            AppendU32(bytes, static_cast<std::uint32_t>(group.ids.size()));
            for (const std::string &id : group.ids)
            {
                if (!AppendString(bytes, id, diagnostic))
                {
                    return false;
                }
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
        std::uint32_t motion_count = 0;
        std::uint32_t expression_count = 0;
        std::uint32_t parameter_group_count = 0;
        if (!ReadU32(bytes, offset, resource.product_version) ||
            !ReadU32(bytes, offset, resource.model3_version) ||
            !ReadU32(bytes, offset, texture_count) ||
            !ReadU32(bytes, offset, chunk_count) ||
            (resource.product_version != kV1ProductVersion &&
             resource.product_version != kV2ProductVersion))
        {
            diagnostic = "Live2D product header is invalid";
            return false;
        }
        if (resource.product_version == kV2ProductVersion &&
            (!ReadU32(bytes, offset, motion_count) ||
             !ReadU32(bytes, offset, expression_count) ||
             !ReadU32(bytes, offset, parameter_group_count)))
        {
            diagnostic = "Live2D Product V2 header is truncated";
            return false;
        }
        if (texture_count == 0 || texture_count > kMaxCollectionCount ||
            chunk_count > kMaxCollectionCount ||
            motion_count > kMaxCollectionCount ||
            expression_count > kMaxCollectionCount ||
            parameter_group_count > kMaxCollectionCount ||
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
        resource.motions.resize(motion_count);
        for (Live2DAuthoredMotion &motion : resource.motions)
        {
            std::uint32_t present = 0;
            if (!ReadString(bytes, offset, motion.group, diagnostic) ||
                !ReadU32(bytes, offset, motion.index) ||
                !ReadU32(bytes, offset, present) ||
                present > 1)
            {
                diagnostic = "Live2D Product V2 motion is invalid";
                return false;
            }
            motion.has_fade_in = present != 0;
            if (motion.has_fade_in && !ReadDouble(bytes, offset, motion.fade_in_time))
            {
                diagnostic = "Live2D Product V2 motion fade is truncated";
                return false;
            }
            if (!ReadU32(bytes, offset, present) || present > 1)
            {
                diagnostic = "Live2D Product V2 motion is invalid";
                return false;
            }
            motion.has_fade_out = present != 0;
            if (motion.has_fade_out && !ReadDouble(bytes, offset, motion.fade_out_time))
            {
                diagnostic = "Live2D Product V2 motion fade is truncated";
                return false;
            }
            if (!ReadBlob(bytes, offset, motion.motion_bytes, diagnostic) ||
                !ReadU32(bytes, offset, present) || present > 1)
            {
                diagnostic = "Live2D Product V2 motion is invalid";
                return false;
            }
            motion.has_sound = present != 0;
            if (motion.has_sound && !ReadBlob(bytes, offset, motion.sound_bytes, diagnostic))
            {
                return false;
            }
        }
        resource.expressions.resize(expression_count);
        for (Live2DAuthoredExpression &expression : resource.expressions)
        {
            if (!ReadString(bytes, offset, expression.name, diagnostic) ||
                !ReadBlob(bytes, offset, expression.expression_bytes, diagnostic))
            {
                return false;
            }
        }
        resource.parameter_groups.resize(parameter_group_count);
        for (Live2DParameterGroup &group : resource.parameter_groups)
        {
            std::uint32_t id_count = 0;
            if (!ReadString(bytes, offset, group.target, diagnostic) ||
                !ReadString(bytes, offset, group.name, diagnostic) ||
                !ReadU32(bytes, offset, id_count) ||
                id_count > kMaxCollectionCount)
            {
                diagnostic = "Live2D Product V2 parameter group is invalid";
                return false;
            }
            group.ids.resize(id_count);
            for (std::string &id : group.ids)
            {
                if (!ReadString(bytes, offset, id, diagnostic))
                {
                    return false;
                }
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
