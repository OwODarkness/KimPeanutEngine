#include "native_texture_loader.h"

#include <fstream>
#include <magic_enum/magic_enum.hpp>
#include <semaphore>
#include <future>
#include <limits>
#include <utility>

#include "asset_product.h"
#include "log/logger.h"
#include "native_texture.h"
#include "texture.h"
#include "utility.h"

namespace kpengine::asset
{
    NativeTextureLoader::NativeTextureLoader(std::filesystem::path product_root)
        : product_root_(std::move(product_root))
    {
    }

    void NativeTextureLoader::SetInitialMipLevelCount(std::uint32_t mip_level_count) noexcept
    {
        initial_mip_level_count_.store(mip_level_count, std::memory_order_release);
    }

    namespace
    {
        std::uintmax_t ProductSize(const std::filesystem::path &path)
        {
            std::error_code size_error;
            const std::uintmax_t product_size = std::filesystem::file_size(path, size_error);
            if (size_error || product_size > kNativeTextureMaxBytes)
            {
                throw NativeTextureError(NativeTextureErrorCode::Overflow,
                                         "native texture product size is invalid");
            }
            return product_size;
        }

        std::vector<std::byte> ReadProductRange(const std::filesystem::path &path,
                                                std::uint64_t offset,
                                                std::uint64_t size)
        {
            const std::uintmax_t product_size = ProductSize(path);
            if (offset > product_size || size > product_size - offset ||
                size > std::numeric_limits<std::size_t>::max())
            {
                throw NativeTextureError(NativeTextureErrorCode::Overflow,
                                         "native texture product range is invalid");
            }

            std::ifstream file(path, std::ios::binary);
            if (!file.is_open())
            {
                throw NativeTextureError(NativeTextureErrorCode::InvalidArgument,
                                         "failed to open native texture product");
            }
            file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
            if (!file)
            {
                throw NativeTextureError(NativeTextureErrorCode::InvalidArgument,
                                         "failed to seek native texture product");
            }
            std::vector<std::byte> bytes(static_cast<std::size_t>(size));
            if (!bytes.empty())
            {
                file.read(reinterpret_cast<char *>(bytes.data()),
                          static_cast<std::streamsize>(bytes.size()));
                if (!file)
                {
                    throw NativeTextureError(NativeTextureErrorCode::InvalidArgument,
                                             "failed to read native texture product");
                }
            }
            return bytes;
        }

        std::uint32_t ReadU32(std::span<const std::byte> bytes, std::size_t offset)
        {
            return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset])) |
                   (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 1])) << 8U) |
                   (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 2])) << 16U) |
                   (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[offset + 3])) << 24U);
        }

        std::uint64_t ReadU64(std::span<const std::byte> bytes, std::size_t offset)
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

        struct NativeTextureDirectory
        {
            std::vector<std::byte> bytes;
            std::uint64_t total_size{};
            std::uint32_t mip_count{};
        };

        NativeTextureDirectory ReadNativeTextureDirectory(const std::filesystem::path &path)
        {
            const std::vector<std::byte> header =
                ReadProductRange(path, 0, kNativeTextureHeaderSize);
            const std::uint64_t total_size = ReadU64(header, 16);
            const std::uint32_t mip_count = ReadU32(header, 32);
            if (mip_count == 0 || mip_count > kNativeTextureMaxMipLevels)
            {
                throw NativeTextureError(NativeTextureErrorCode::InvalidDirectory,
                                         "native texture mip count is invalid");
            }
            const std::uint64_t directory_size =
                static_cast<std::uint64_t>(mip_count) * kNativeTextureMipEntrySize;
            if (directory_size > std::numeric_limits<std::uint64_t>::max() -
                                      kNativeTextureHeaderSize)
            {
                throw NativeTextureError(NativeTextureErrorCode::Overflow,
                                         "native texture directory size overflows");
            }
            const std::uint64_t directory_end = kNativeTextureHeaderSize + directory_size;
            if (directory_end > total_size)
            {
                throw NativeTextureError(NativeTextureErrorCode::InvalidDirectory,
                                         "native texture directory is outside the product");
            }
            if (ProductSize(path) != total_size)
            {
                throw NativeTextureError(NativeTextureErrorCode::InvalidDirectory,
                                         "native texture file size does not match its header");
            }
            return {ReadProductRange(path, 0, directory_end), total_size, mip_count};
        }

        std::filesystem::path ResolveVerificationRoot(
            const std::filesystem::path &product_path,
            const std::filesystem::path &product_root)
        {
            std::filesystem::path verification_root = product_root;
            if (!verification_root.empty() &&
                product_path.parent_path() != verification_root / "textures")
            {
                const std::filesystem::path candidate_root =
                    product_path.parent_path().parent_path();
                if (product_path.parent_path().filename() == "textures" &&
                    candidate_root.filename() == ".archive")
                {
                    verification_root = candidate_root;
                }
            }
            return verification_root;
        }

        NativeTextureProduct ReadAndDecode(const std::filesystem::path &product_path,
                                           const std::filesystem::path &product_root,
                                           std::uint32_t first_mip_level)
        {
            const std::vector<std::byte> bytes = ReadProductRange(
                product_path, 0, ProductSize(product_path));
            const auto hashes = Sha256WithZeroedRange(bytes, kNativeTextureDigestOffset,
                                                      kNativeTextureDigestSize);
            if (!hashes)
            {
                throw NativeTextureError(NativeTextureErrorCode::Truncated,
                                         "native texture digest is truncated");
            }
            std::string diagnostic;
            if (!VerifyArchiveProduct(product_path, ArchiveProductType::Texture, bytes,
                                       diagnostic,
                                       ResolveVerificationRoot(product_path, product_root),
                                       hashes->content_hash))
            {
                throw NativeTextureError(NativeTextureErrorCode::IntegrityMismatch,
                                         "invalid native texture archive product: " + diagnostic);
            }
            return DeserializeNativeTexture(bytes, &*hashes, first_mip_level);
        }

        NativeTextureProduct ReadInitialAndDecode(const std::filesystem::path &product_path,
                                                  std::uint32_t first_mip_level)
        {
            const NativeTextureDirectory directory = ReadNativeTextureDirectory(product_path);
            const std::size_t entry_offset = kNativeTextureHeaderSize +
                                              static_cast<std::size_t>(first_mip_level) *
                                                  kNativeTextureMipEntrySize;
            if (entry_offset + kNativeTextureMipEntrySize > directory.bytes.size())
            {
                throw NativeTextureError(NativeTextureErrorCode::InvalidDirectory,
                                         "native texture resident mip entry is invalid");
            }
            const std::uint64_t payload_offset = ReadU64(directory.bytes, entry_offset + 12);
            if (payload_offset > directory.total_size)
            {
                throw NativeTextureError(NativeTextureErrorCode::InvalidDirectory,
                                         "native texture resident payload offset is invalid");
            }
            const std::vector<std::byte> payload = ReadProductRange(
                product_path, payload_offset, directory.total_size - payload_offset);
            return DeserializeNativeTextureRange(directory.bytes, directory.total_size,
                                                 first_mip_level, payload, payload_offset);
        }

        std::shared_ptr<const TextureData> LoadFullResolutionData(
            std::filesystem::path product_path, std::filesystem::path product_root)
        {
            static std::counting_semaphore<2> slots{2};
            slots.acquire();
            try
            {
                NativeTextureProduct product = ReadAndDecode(product_path, product_root, 0);
                auto result = std::make_shared<TextureData>(std::move(product.data));
                slots.release();
                return result;
            }
            catch (...)
            {
                slots.release();
                throw;
            }
        }
    }

    bool NativeTextureLoader::Load(const std::string &path, AssetRegisterInfo &info) const
    {
        try
        {
            const std::filesystem::path product_path{path};
            const NativeTextureDirectory directory = ReadNativeTextureDirectory(product_path);
            const std::uint32_t mip_count = directory.mip_count;
            const std::uint32_t requested_levels =
                initial_mip_level_count_.load(std::memory_order_acquire);
            const std::uint32_t format_value =
                ReadU32(directory.bytes, 36);
            const std::uint32_t first_mip_level = format_value !=
                                                          static_cast<std::uint32_t>(TextureFormat::TEXTURE_FORMAT_RGBA16F) &&
                                                       requested_levels != 0 &&
                                                          requested_levels < mip_count
                                                      ? mip_count - requested_levels
                                                      : 0;
            NativeTextureProduct product = first_mip_level == 0
                                               ? ReadAndDecode(product_path, product_root_, 0)
                                               : ReadInitialAndDecode(product_path, first_mip_level);
            auto texture = std::make_shared<TextureResource>();
            texture->channel_count = 4;
            *texture->data = std::move(product.data);
            if (first_mip_level != 0)
            {
                texture->SetFullResolutionLoader(
                    [product_path, product_root = product_root_]()
                    { return LoadFullResolutionData(product_path, product_root); });
            }
            info.type = AssetType::KPAT_Texture;
            info.path = path;
            info.name = std::string{magic_enum::enum_name(info.type)} + "_" +
                        ExtractNameFromPath(path);
            info.resource = std::move(texture);
            return true;
        }
        catch (const NativeTextureError &error)
        {
            KP_LOG("NativeTextureLoadLog", LOG_LEVEL_ERROR, "%s: %s", path.c_str(), error.what());
            return false;
        }
    }
}
