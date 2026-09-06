#ifndef KPENGINE_RUNTIME_ASSET_ASSET_PRODUCT_H
#define KPENGINE_RUNTIME_ASSET_ASSET_PRODUCT_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace kpengine::asset
{
    constexpr std::size_t kModelArchiveHashSize = 32;
    using ModelArchiveHashBytes = std::array<std::uint8_t, kModelArchiveHashSize>;

    struct ContentHash
    {
        ModelArchiveHashBytes bytes{};

        std::string ToHex() const;
        static std::optional<ContentHash> FromHex(std::string_view value);

        friend bool operator==(const ContentHash &lhs, const ContentHash &rhs) noexcept
        {
            return lhs.bytes == rhs.bytes;
        }

        friend bool operator!=(const ContentHash &lhs, const ContentHash &rhs) noexcept
        {
            return !(lhs == rhs);
        }

        friend bool operator<(const ContentHash &lhs, const ContentHash &rhs) noexcept
        {
            return lhs.bytes < rhs.bytes;
        }
    };

    enum class ModelArchiveErrorCode : std::uint8_t
    {
        InvalidArgument,
        IoError,
        ArchiveBusy,
        NewerSchema,
        InvalidDatabase,
        SourceNotFound,
        MissingProduct,
        CorruptProduct,
    };

    // This error type is shared by the database-backed importer and the
    // database-free product/hash layer. The latter never opens SQLite.
    class ModelArchiveError final : public std::runtime_error
    {
    public:
        ModelArchiveError(ModelArchiveErrorCode code, std::string message,
                          int sqlite_result_code = 0);

        ModelArchiveErrorCode Code() const noexcept;
        int SqliteResultCode() const noexcept;

    private:
        ModelArchiveErrorCode code_{};
        int sqlite_result_code_{};
    };

    ContentHash Sha256(std::string_view value);
    ContentHash Sha256(const std::vector<std::byte> &value);
    ContentHash Sha256File(const std::filesystem::path &path);

    // Returns a portable, lower-case Asset-relative path. Absolute paths and
    // traversal outside the Asset root are rejected at the archive boundary.
    std::string NormalizeAssetRelativePath(std::string_view path);

    struct SourceFingerprintInput
    {
        std::string normalized_path;
        ContentHash content_hash;
    };

    ContentHash HashSourcePackage(const std::vector<SourceFingerprintInput> &files);

    struct ImportKeyInput
    {
        ContentHash source_package_hash;
        std::string importer_id;
        std::uint32_t importer_version{};
        ContentHash settings_hash;
        std::uint32_t native_model_version{};
        std::uint32_t material_schema_version{};
    };

    ContentHash HashImportKey(const ImportKeyInput &input);

    enum class ArchiveProductType : std::uint8_t
    {
        Model = 1,
        Material = 2,
        Texture = 3,
    };

    // Verifies an archive product's canonical location and content-addressed
    // filename. With product_root supplied, the path must be directly under
    // that root's canonical product directory; without it, paths outside a
    // .archive root are authoring/fixture paths and are intentionally exempt.
    bool VerifyArchiveProduct(const std::filesystem::path &path,
                              ArchiveProductType type,
                              const std::vector<std::byte> &bytes,
                              std::string &diagnostic,
                              const std::filesystem::path &product_root = {});

    std::string ProductRelativePath(ArchiveProductType type, const ContentHash &content_hash,
                                    std::string_view texture_extension = {});
}

#endif
