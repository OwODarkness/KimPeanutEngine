#include "native_texture_loader.h"

#include <fstream>
#include <magic_enum/magic_enum.hpp>
#include <semaphore>
#include <future>
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
        std::vector<std::byte> ReadProduct(const std::filesystem::path &path)
        {
            std::ifstream file(path, std::ios::binary | std::ios::ate);
            if (!file.is_open())
            {
                throw NativeTextureError(NativeTextureErrorCode::InvalidArgument,
                                         "failed to open native texture product");
            }
            const std::streampos end = file.tellg();
            if (end < 0 || static_cast<std::uintmax_t>(end) > kNativeTextureMaxBytes)
            {
                throw NativeTextureError(NativeTextureErrorCode::Overflow,
                                         "native texture product size is invalid");
            }
            std::vector<std::byte> bytes(static_cast<std::size_t>(end));
            file.seekg(0, std::ios::beg);
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

        std::uint32_t ReadMipCount(std::span<const std::byte> bytes)
        {
            if (bytes.size() < kNativeTextureHeaderSize)
            {
                throw NativeTextureError(NativeTextureErrorCode::Truncated,
                                         "native texture header is truncated");
            }
            return static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[32])) |
                   (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[33])) << 8U) |
                   (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[34])) << 16U) |
                   (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[35])) << 24U);
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
            const std::vector<std::byte> bytes = ReadProduct(product_path);
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
            const std::vector<std::byte> bytes = ReadProduct(product_path);
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
                                       ResolveVerificationRoot(product_path, product_root_),
                                       hashes->content_hash))
            {
                throw NativeTextureError(NativeTextureErrorCode::IntegrityMismatch,
                                         "invalid native texture archive product: " + diagnostic);
            }
            const std::uint32_t mip_count = ReadMipCount(bytes);
            const std::uint32_t requested_levels =
                initial_mip_level_count_.load(std::memory_order_acquire);
            const std::uint32_t format_value =
                static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[36])) |
                (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[37])) << 8U) |
                (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[38])) << 16U) |
                (static_cast<std::uint32_t>(std::to_integer<std::uint8_t>(bytes[39])) << 24U);
            const std::uint32_t first_mip_level = format_value !=
                                                          static_cast<std::uint32_t>(TextureFormat::TEXTURE_FORMAT_RGBA16F) &&
                                                      requested_levels != 0 &&
                                                          requested_levels < mip_count
                                                      ? mip_count - requested_levels
                                                      : 0;
            NativeTextureProduct product =
                DeserializeNativeTexture(bytes, &*hashes, first_mip_level);
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
