#include "native_texture_loader.h"

#include <fstream>
#include <magic_enum/magic_enum.hpp>
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
    }

    bool NativeTextureLoader::Load(const std::string &path, AssetRegisterInfo &info) const
    {
        try
        {
            const std::filesystem::path product_path{path};
            const std::vector<std::byte> bytes = ReadProduct(product_path);
            std::string diagnostic;
            if (!VerifyArchiveProduct(product_path, ArchiveProductType::Texture, bytes,
                                       diagnostic, product_root_))
            {
                throw NativeTextureError(NativeTextureErrorCode::IntegrityMismatch,
                                         "invalid native texture archive product: " + diagnostic);
            }
            NativeTextureProduct product = DeserializeNativeTexture(bytes);
            auto texture = std::make_shared<TextureResource>();
            texture->channel_count = 4;
            *texture->data = std::move(product.data);
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
