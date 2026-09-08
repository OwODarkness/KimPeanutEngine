#ifndef KPENGINE_RUNTIME_ASSET_TEXTURE_IMPORTER_H
#define KPENGINE_RUNTIME_ASSET_TEXTURE_IMPORTER_H

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

#include "asset_product.h"
#include "data/texture.h"
#include "image_io/image_io.h"
#include "native_texture.h"

namespace kpengine::asset
{
    enum class TextureCompressionPolicy : std::uint8_t
    {
        Portable,
        RequireBlockCompression,
    };

    struct TextureCookSettings
    {
        data::TextureSemantic semantic{data::TextureSemantic::Generic};
        std::uint32_t max_dimension{2048};
        std::uint32_t max_levels{};
        TextureCompressionPolicy compression{TextureCompressionPolicy::Portable};
    };

    struct TextureImportRequest
    {
        std::filesystem::path source_path;
        TextureCookSettings settings{};
    };

    struct ImportedTexture
    {
        ContentHash source_hash{};
        image_io::ImageBuffer image;
        TextureCookSettings settings{};
    };

    struct CookedTexture
    {
        data::TextureData data;
        std::vector<std::byte> bytes;
        ContentHash product_hash{};
    };

    enum class TextureCookErrorCode : std::uint8_t
    {
        InvalidArgument,
        IoError,
        DecodeFailed,
        UnsupportedCompression,
        ConversionFailed,
        ProductInvalid,
    };

    class TextureCookError final : public std::runtime_error
    {
    public:
        TextureCookError(TextureCookErrorCode code, std::string message);

        TextureCookErrorCode Code() const noexcept;

    private:
        TextureCookErrorCode code_{};
    };

    // Database-free source importer. It performs only source decoding and
    // records no AssetManager identity or runtime Asset state.
    class TextureImporter final
    {
    public:
        ImportedTexture Import(const TextureImportRequest &request) const;
    };

    // Database-free cooker. It turns decoded CPU pixels into deterministic,
    // bounded native bytes and owns no archive or runtime state.
    class TextureCooker final
    {
    public:
        CookedTexture Cook(const ImportedTexture &source) const;
    };

    ImportedTexture ImportTexture(const TextureImportRequest &request);
    CookedTexture CookTexture(const ImportedTexture &source);

    // Product publication is an offline AssetImport operation. Runtime never
    // calls this function and never writes the archive.
    void PublishCookedTextureProduct(const std::filesystem::path &archive_root,
                                     const CookedTexture &cooked);
}

#endif
