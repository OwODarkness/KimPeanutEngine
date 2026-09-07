#ifndef KPENGINE_RUNTIME_ASSET_NATIVE_MATERIAL_H
#define KPENGINE_RUNTIME_ASSET_NATIVE_MATERIAL_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "image_io/image_io.h"
#include "imported_model.h"
#include "material.h"
#include "asset_product.h"
#include "texture_importer.h"

namespace kpengine::asset
{
    constexpr int kNativeMaterialSchemaVersion = 2;

    struct NativeMaterialConversionSettings
    {
        std::filesystem::path asset_root;
        std::string shader_asset_path{"shader/pbr_gbuffer.shader"};
        TextureCookSettings texture_settings{};
        std::function<void(std::string_view)> texture_progress_callback;
    };

    struct NativeImageProduct
    {
        ContentHash content_hash;
        std::string extension;
        std::vector<std::byte> bytes;
        image_io::ImageBuffer decoded_image;
    };

    struct NativeMaterialProduct
    {
        std::size_t source_material_index{};
        std::string display_name;
        MaterialResource material;
        std::vector<std::byte> bytes;
        ContentHash content_hash;
    };

    struct NativeMaterialConversionResult
    {
        std::vector<NativeMaterialProduct> materials;
        std::vector<NativeImageProduct> embedded_images;
    };

    enum class NativeMaterialErrorCode : std::uint8_t
    {
        InvalidArgument,
        UnsupportedSemantics,
        MissingImage,
        MalformedImage,
        InvalidValue,
    };

    class NativeMaterialConversionError final : public std::runtime_error
    {
    public:
        NativeMaterialConversionError(NativeMaterialErrorCode code, std::string message);

        NativeMaterialErrorCode Code() const noexcept;

    private:
        NativeMaterialErrorCode code_{};
    };

    // Pure, offline material conversion. It does not access AssetManager,
    // Runtime, Render, Graphics, or an archive database.
    NativeMaterialConversionResult ConvertImportedMaterials(
        const ImportedModelDocument &document,
        const NativeMaterialConversionSettings &settings);

    // Validates canonical material product bytes without opening the runtime
    // AssetManager or requiring a product file on disk.
    void ValidateNativeMaterialProduct(const std::vector<std::byte> &bytes);
}

#endif
