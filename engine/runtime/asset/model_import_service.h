#ifndef KPENGINE_RUNTIME_ASSET_MODEL_IMPORT_SERVICE_H
#define KPENGINE_RUNTIME_ASSET_MODEL_IMPORT_SERVICE_H

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "model_archive.h"
#include "texture_importer.h"

namespace kpengine::asset
{
    enum class ModelImportMetricStage : std::uint8_t
    {
        CacheProbe,
        SourceDecode,
        DependencyHash,
        TextureCook,
        ProductSerialize,
        ProductValidate,
        ProductHash,
        StagingWrite,
        Publication,
        ArchiveCommit,
        Count,
    };

    struct ModelImportMetrics
    {
        std::array<double, static_cast<std::size_t>(ModelImportMetricStage::Count)>
            stage_seconds{};
        double total_seconds{};
        double process_cpu_seconds{};
        double cpu_utilization_percent{};
        double storage_write_megabytes_per_second{};
        std::uint32_t logical_processor_count{};
        std::uint32_t texture_worker_count{};
        std::uint32_t completion_queue_capacity{};
        TextureBcEncoder bc_encoder{TextureBcEncoder::ReferenceV1};
        TextureBcQuality bc_quality{TextureBcQuality::Balanced};

        std::uint64_t source_image_count{};
        std::uint64_t requested_texture_bindings{};
        std::uint64_t unique_cook_keys{};
        std::uint64_t texture_decode_count{};
        std::uint64_t texture_prepare_count{};
        std::uint64_t texture_cook_count{};
        std::uint64_t portable_encode_count{};
        std::uint64_t block_encode_count{};
        TextureBcEncodingMetrics bc_encoding{};
        std::uint64_t unique_texture_product_count{};
        std::uint64_t texture_product_bytes{};
        std::uint64_t cache_hit_count{};
        std::uint64_t product_count{};
        std::uint64_t product_write_count{};
        std::uint64_t source_bytes_read{};
        std::uint64_t product_bytes_read{};
        std::uint64_t bytes_written{};
        std::uint64_t peak_working_set_bytes{};
        std::uint64_t peak_reserved_bytes{};
        std::uint64_t peak_active_jobs{};
        std::uint64_t texture_memory_budget_bytes{};
        std::uint64_t current_reserved_bytes{};
        std::uint64_t estimated_texture_bytes{};
        std::uint64_t actual_texture_bytes{};
        std::uint64_t oversized_job_count{};
        std::uint64_t memory_estimate_correction_count{};
        std::uint64_t completed_texture_jobs{};
        std::uint64_t total_texture_jobs{};
        std::uint64_t peak_completion_queue_size{};
        double worker_memory_wait_seconds{};
        double worker_queue_wait_seconds{};
        double coordinator_wait_seconds{};
        bool has_memory_budget{false};
        bool cache_hit{false};
    };

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
        Cancelled,
    };

    class ModelImportError final : public std::runtime_error
    {
    public:
        ModelImportError(ModelImportErrorCode code, std::string message);

        ModelImportErrorCode Code() const noexcept;

    private:
        ModelImportErrorCode code_{};
    };

    enum class ModelImportProgressStage : std::uint8_t
    {
        CheckingCache,
        DecodingSource,
        HashingDependencies,
        CookingTextures,
        SerializingProducts,
        PublishingProducts,
        UpdatingArchive,
        Complete,
    };

    struct ModelImportProgress
    {
        ModelImportProgressStage stage{ModelImportProgressStage::CheckingCache};
        std::string message;
        std::size_t completed{};
        std::size_t total{};
    };

    using ModelImportProgressCallback = std::function<void(const ModelImportProgress &)>;

    struct ModelImportSettings
    {
        std::string importer_id{"assimp"};
        std::uint32_t importer_version{1};
        std::uint32_t native_model_version{3};
        std::uint32_t material_schema_version{2};
        std::string shader_asset_path{"shader/pbr_gbuffer.shader"};
        TextureCookSettings texture_settings{};
        bool emit_texture_profile_variants{true};
    };

    struct ModelImportExecutionPolicy
    {
        std::uint32_t texture_worker_count{};
        std::uint64_t texture_memory_budget_bytes{};
        std::uint32_t completion_queue_capacity{2};
        std::function<bool()> cancellation_requested;
        // Test/diagnostic seam invoked by the coordinator before consuming a
        // completion. It is intentionally not exposed by the AssetTool CLI.
        std::function<void()> before_completion_consume;
    };

    struct ModelImportRequest
    {
        std::filesystem::path asset_root;
        std::filesystem::path archive_root;
        std::filesystem::path source_path;
        ModelImportSettings settings{};
        ModelImportProgressCallback progress_callback;
        ModelImportExecutionPolicy execution{};
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
        ModelImportMetrics metrics{};
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
