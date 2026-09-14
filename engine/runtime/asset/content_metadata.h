#pragma once

#include "asset_product.h"

#include <filesystem>
#include <string>
#include <vector>
#include <utility>

namespace kpengine::asset
{

class ContentID
{
public:
    ContentID() = default;
    explicit ContentID(std::string value) : value_(std::move(value)) {}

    [[nodiscard]] const std::string& ToString() const noexcept { return value_; }
    [[nodiscard]] bool IsValid() const noexcept { return !value_.empty(); }

    friend bool operator==(const ContentID& lhs, const ContentID& rhs) noexcept
    {
        return lhs.value_ == rhs.value_;
    }

    friend bool operator!=(const ContentID& lhs, const ContentID& rhs) noexcept
    {
        return !(lhs == rhs);
    }

    friend bool operator<(const ContentID& lhs, const ContentID& rhs) noexcept
    {
        return lhs.value_ < rhs.value_;
    }

private:
    std::string value_;
};

[[nodiscard]] ContentID MakeContentID(const std::string& type_name,
                                      const std::string& logical_key);

enum class ContentImportStatus : unsigned char
{
    Ready,
    Stale,
    Failed,
    MissingProduct,
    Orphaned,
};

enum class ContentVisibility : unsigned char
{
    Visible,
    Internal,
};

struct ContentReference
{
    std::string slot;
    ContentID target;
};

struct ContentProduct
{
    ArchiveProductType type = ArchiveProductType::Model;
    ContentHash hash{};
};

struct ContentMetadata
{
    int schema_version = 1;
    ContentID id;
    std::string type_name;
    std::string name;
    // A project-relative path without the .kpmeta suffix.
    std::string content_path;
    std::string source_path;
    ContentImportStatus status = ContentImportStatus::Ready;
    ContentVisibility visibility = ContentVisibility::Visible;
    std::vector<ContentReference> references;
    std::vector<ContentProduct> products;
};

[[nodiscard]] std::filesystem::path MetadataPath(const std::filesystem::path& content_root,
                                                  const std::string& content_path);

bool WriteContentMetadata(const std::filesystem::path& content_root,
                          const ContentMetadata& metadata,
                          std::string* diagnostic = nullptr);

[[nodiscard]] bool ReadContentMetadata(const std::filesystem::path& metadata_path,
                                       ContentMetadata& metadata,
                                       std::string* diagnostic = nullptr);

struct ContentRegistrySnapshot
{
    std::vector<ContentMetadata> records;
    std::vector<std::string> diagnostics;
};

class ContentRegistry
{
public:
    explicit ContentRegistry(std::filesystem::path content_root);

    // Capture is explicit so runtime startup never scans the editor content tree.
    [[nodiscard]] ContentRegistrySnapshot Capture() const;

private:
    std::filesystem::path content_root_;
};

} // namespace kpengine::asset
