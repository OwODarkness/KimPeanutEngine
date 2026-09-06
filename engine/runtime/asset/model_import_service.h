#ifndef KPENGINE_RUNTIME_ASSET_MODEL_IMPORT_SERVICE_H
#define KPENGINE_RUNTIME_ASSET_MODEL_IMPORT_SERVICE_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "model_archive.h"

namespace kpengine::asset
{
    enum class ModelImportStatus : std::uint8_t
    {
        Imported,
        UpToDate,
    };

    enum class ModelImportErrorCode : std::uint8_t
    {
        InvalidArgument,
        IoError,
        ArchiveBusy,
        DecodeFailed,
        ConversionFailed,
        ProductInvalid,
        ProductCollision,
        PublicationFailed,
        ArchiveCommitFailed,
    };

    class ModelImportError final : public std::runtime_error
    {
    public:
        ModelImportError(ModelImportErrorCode code, std::string message);

        ModelImportErrorCode Code() const noexcept;

    private:
        ModelImportErrorCode code_{};
    };

    struct ModelImportSettings
    {
        std::string importer_id{"assimp"};
        std::uint32_t importer_version{1};
        std::uint32_t native_model_version{1};
        std::uint32_t material_schema_version{2};
        std::string shader_asset_path{"shader/pbr_gbuffer.shader"};
    };

    struct ModelImportRequest
    {
        std::filesystem::path asset_root;
        std::filesystem::path archive_root;
        std::filesystem::path source_path;
        ModelImportSettings settings{};
    };

    struct ModelImportResult
    {
        ModelImportStatus status{ModelImportStatus::Imported};
        std::string normalized_source_path;
        ContentHash source_package_hash{};
        ContentHash model_hash{};
        std::filesystem::path model_path;
        std::vector<ContentHash> material_hashes;
        std::vector<ContentHash> texture_hashes;
    };

    // Offline authoring service. It owns no runtime Asset identity and does
    // not require the engine application, AssetManager, Render, or Graphics.
    class ModelImportService final
    {
    public:
        explicit ModelImportService(std::int32_t archive_busy_timeout_ms = 2500);
        ~ModelImportService() noexcept;

        ModelImportService(const ModelImportService &) = delete;
        ModelImportService &operator=(const ModelImportService &) = delete;

        ModelImportResult Import(const ModelImportRequest &request);

    private:
        struct Impl;

        std::unique_ptr<Impl> impl_;
    };

    ModelImportResult ImportModel(const ModelImportRequest &request);
}

#endif
