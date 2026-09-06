#ifndef KPENGINE_RUNTIME_ASSET_MATERIAL_PROMOTION_H
#define KPENGINE_RUNTIME_ASSET_MATERIAL_PROMOTION_H

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "asset_product.h"

namespace kpengine::asset
{
    enum class MaterialPromotionErrorCode : std::uint8_t
    {
        InvalidArgument,
        SourceNotFound,
        ProductMissing,
        IoError,
        Collision,
        ArchiveCommitFailed,
    };

    class MaterialPromotionError final : public std::runtime_error
    {
    public:
        MaterialPromotionError(MaterialPromotionErrorCode code, std::string message);

        MaterialPromotionErrorCode Code() const noexcept;

    private:
        MaterialPromotionErrorCode code_{};
    };

    struct MaterialPromotionRequest
    {
        std::filesystem::path asset_root;
        std::filesystem::path archive_root;
        std::string logical_model_path;
        std::uint32_t material_slot{};
        std::filesystem::path authored_material_path;
    };

    struct MaterialPromotionResult
    {
        ContentHash generated_material_hash{};
        std::filesystem::path authored_material_path;
        bool authored_file_created{};
    };

    // Copies one immutable generated Material to an authored path and records
    // the explicit Level/Model slot override in the archive. The generated
    // product remains immutable and is never replaced or deleted.
    MaterialPromotionResult PromoteGeneratedMaterial(
        const MaterialPromotionRequest &request);
}

#endif
