#ifndef KPENGINE_RUNTIME_ASSET_MODEL_ARCHIVE_H
#define KPENGINE_RUNTIME_ASSET_MODEL_ARCHIVE_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "asset_product.h"

namespace kpengine::database
{
    class Database;
}

namespace kpengine::asset
{
    enum class ModelArchiveOpenMode
    {
        ReadWrite,
        ReadOnly,
    };

    enum class SourceImportStatus : std::int32_t
    {
        Failed = 0,
        Ready = 1,
    };

    struct SourceRecord
    {
        std::int64_t id{};
        std::string normalized_path;
        ContentHash path_hash;
        std::string display_name;
        ContentHash package_hash;
        std::string importer_id;
        std::uint32_t importer_version{};
        ContentHash settings_hash;
        std::uint32_t native_model_version{};
        SourceImportStatus status{SourceImportStatus::Ready};
        std::string diagnostic;
    };

    struct SourceDependencyRecord
    {
        std::string normalized_path;
        ContentHash content_hash;
    };

    struct ProductRecord
    {
        ContentHash content_hash;
        ArchiveProductType asset_type{ArchiveProductType::Model};
        std::string relative_path;
        std::uint64_t byte_size{};
        std::uint32_t schema_version{};
    };

    struct SourceProductRecord
    {
        ContentHash content_hash;
        ArchiveProductType asset_type{ArchiveProductType::Model};
        std::int32_t role{};
        std::int32_t slot{-1};
        std::string display_name;
    };

    struct MaterialOverrideRecord
    {
        std::int32_t slot{};
        std::string authored_path;
    };

    struct SourceArchiveSnapshot
    {
        SourceRecord source;
        std::vector<SourceDependencyRecord> dependencies;
        std::vector<ProductRecord> products;
        std::vector<SourceProductRecord> source_products;
        std::vector<MaterialOverrideRecord> material_overrides;
    };

    enum class ArchiveProbeStatus : std::uint8_t
    {
        SourceNotFound,
        SourceFailed,
        SourcePackageChanged,
        ImporterChanged,
        SettingsChanged,
        NativeSchemaChanged,
        MissingProduct,
        CorruptProduct,
        UpToDate,
    };

    struct SourceProbeRequest
    {
        std::string normalized_path;
        ContentHash package_hash;
        std::string importer_id;
        std::uint32_t importer_version{};
        ContentHash settings_hash;
        std::uint32_t native_model_version{};
    };

    struct ArchiveProbeResult
    {
        ArchiveProbeStatus status{ArchiveProbeStatus::SourceNotFound};
        std::string diagnostic;
        std::optional<SourceArchiveSnapshot> snapshot;
    };

    class ModelArchiveDatabase final
    {
    public:
        static constexpr std::int32_t kSchemaVersion = 1;

        explicit ModelArchiveDatabase(
            std::filesystem::path database_path,
            std::int32_t busy_timeout_ms = 2500,
            ModelArchiveOpenMode open_mode = ModelArchiveOpenMode::ReadWrite);
        ~ModelArchiveDatabase() noexcept;

        ModelArchiveDatabase(ModelArchiveDatabase &&other) noexcept;
        ModelArchiveDatabase &operator=(ModelArchiveDatabase &&other) noexcept;

        ModelArchiveDatabase(const ModelArchiveDatabase &) = delete;
        ModelArchiveDatabase &operator=(const ModelArchiveDatabase &) = delete;

        const std::filesystem::path &DatabasePath() const noexcept;
        std::filesystem::path ArchiveRoot() const;
        std::int32_t SchemaVersion() const noexcept;

        std::optional<SourceArchiveSnapshot> FindSource(std::string_view normalized_path);
        std::optional<SourceArchiveSnapshot> FindSourceByLogicalPath(
            std::string_view logical_path);
        std::optional<std::filesystem::path> ResolveModelProductPath(
            std::string_view logical_path);

        // Product bytes must already be staged under ArchiveRoot(). The
        // operation verifies them before the short metadata transaction.
        void ReplaceSource(const SourceRecord &source,
                           const std::vector<SourceDependencyRecord> &dependencies,
                           const std::vector<ProductRecord> &products,
                           const std::vector<SourceProductRecord> &source_products,
                           const std::vector<MaterialOverrideRecord> &material_overrides);

        ArchiveProbeResult ProbeSource(const SourceProbeRequest &request);
        void IntegrityCheck();

    private:
        struct Impl;

        void Initialize(std::int32_t busy_timeout_ms);
        void EnsureSchema();
        void ConfigureConnection(std::int32_t busy_timeout_ms);
        void VerifyProductFile(const ProductRecord &product) const;

        std::filesystem::path database_path_;
        ModelArchiveOpenMode open_mode_{ModelArchiveOpenMode::ReadWrite};
        std::unique_ptr<Impl> impl_{};
    };
}

#endif
