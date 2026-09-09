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
        bool emit_texture_profile_variants{false};
        std::function<void(std::string_view)> texture_progress_callback;
    };

    struct NativeImageProduct
    {
        ContentHash content_hash;
        std::string extension;
        std::vector<std::byte> bytes;
    };

    struct NativeMaterialConversionMetrics
    {
        std::uint64_t requested_texture_bindings{};
        std::uint64_t unique_cook_keys{};
        std::uint64_t texture_decode_count{};
        std::uint64_t texture_prepare_count{};
        std::uint64_t texture_cook_count{};
        std::uint64_t portable_encode_count{};
        std::uint64_t block_encode_count{};
        std::uint64_t unique_texture_product_count{};
        std::uint64_t texture_product_bytes{};
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
        NativeMaterialConversionMetrics metrics{};
    };

    struct NativeMaterialTextureBindingPlan
    {
        std::string name;
        MaterialTextureColorSpace color_space{MaterialTextureColorSpace::Linear};
        MaterialTextureChannel channel{MaterialTextureChannel::Rgba};
        std::size_t job_ordinal{};
    };

    struct NativeMaterialMaterialPlan
    {
        std::size_t source_material_index{};
        std::vector<NativeMaterialTextureBindingPlan> texture_bindings;
    };

    struct NativeTextureCookJob
    {
        std::size_t image_index{};
        data::TextureSemantic semantic{data::TextureSemantic::Generic};
    };

    struct NativeTextureCookResult
    {
        std::size_t job_ordinal{};
        std::string portable_path;
        std::string block_compressed_path;
        std::vector<NativeImageProduct> products;
        NativeMaterialConversionMetrics metrics{};
        std::uint64_t estimated_bytes{};
        std::uint64_t actual_bytes{};
    };

    struct NativeMaterialCookPlan
    {
        std::vector<NativeMaterialMaterialPlan> materials;
        std::vector<NativeTextureCookJob> texture_jobs;
        std::uint64_t requested_texture_bindings{};
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

    NativeMaterialCookPlan BuildNativeMaterialCookPlan(
        const ImportedModelDocument &document,
        const NativeMaterialConversionSettings &settings);

    NativeTextureCookResult ExecuteNativeTextureCookJob(
        const ImportedModelDocument &document,
        const NativeMaterialConversionSettings &settings,
        const NativeTextureCookJob &job,
        std::size_t job_ordinal);

    NativeMaterialConversionResult FinalizeNativeMaterials(
        const ImportedModelDocument &document,
        const NativeMaterialCookPlan &plan,
        const std::vector<NativeTextureCookResult> &texture_results,
        const NativeMaterialConversionSettings &settings);

    // Validates canonical material product bytes without opening the runtime
    // AssetManager or requiring a product file on disk.
    void ValidateNativeMaterialProduct(const std::vector<std::byte> &bytes);
}

#endif
