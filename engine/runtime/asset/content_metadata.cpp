#include "content_metadata.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>
#include <utility>

namespace kpengine::asset
{
namespace
{

using Json = nlohmann::json;

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

std::uint64_t StableHash(const std::string& value, std::uint64_t seed)
{
    std::uint64_t hash = kFnvOffset ^ seed;
    for (const unsigned char character : value)
    {
        hash ^= character;
        hash *= kFnvPrime;
    }
    return hash;
}

std::string StatusName(const ContentImportStatus status)
{
    switch (status)
    {
    case ContentImportStatus::Ready: return "ready";
    case ContentImportStatus::Stale: return "stale";
    case ContentImportStatus::Failed: return "failed";
    case ContentImportStatus::MissingProduct: return "missing_product";
    case ContentImportStatus::Orphaned: return "orphaned";
    }
    return "failed";
}

bool ParseStatus(const std::string& value, ContentImportStatus& status)
{
    if (value == "ready") status = ContentImportStatus::Ready;
    else if (value == "stale") status = ContentImportStatus::Stale;
    else if (value == "failed") status = ContentImportStatus::Failed;
    else if (value == "missing_product") status = ContentImportStatus::MissingProduct;
    else if (value == "orphaned") status = ContentImportStatus::Orphaned;
    else return false;
    return true;
}

std::string VisibilityName(const ContentVisibility visibility)
{
    return visibility == ContentVisibility::Visible ? "visible" : "internal";
}

bool ParseVisibility(const std::string& value, ContentVisibility& visibility)
{
    if (value == "visible") visibility = ContentVisibility::Visible;
    else if (value == "internal") visibility = ContentVisibility::Internal;
    else return false;
    return true;
}

Json ToJson(const ContentMetadata& metadata)
{
    Json references = Json::array();
    for (const ContentReference& reference : metadata.references)
    {
        references.push_back({{"slot", reference.slot}, {"target", reference.target.ToString()}});
    }

    Json products = Json::array();
    for (const ContentProduct& product : metadata.products)
    {
        products.push_back({{"type", static_cast<int>(product.type)}, {"hash", product.hash.ToHex()}});
    }

    return {
        {"schema_version", metadata.schema_version},
        {"id", metadata.id.ToString()},
        {"type", metadata.type_name},
        {"name", metadata.name},
        {"content_path", metadata.content_path},
        {"source_path", metadata.source_path},
        {"status", StatusName(metadata.status)},
        {"visibility", VisibilityName(metadata.visibility)},
        {"references", references},
        {"products", products},
    };
}

bool FromJson(const Json& json, ContentMetadata& metadata, std::string* diagnostic)
{
    try
    {
        metadata.schema_version = json.value("schema_version", 1);
        metadata.id = ContentID(json.at("id").get<std::string>());
        metadata.type_name = json.at("type").get<std::string>();
        metadata.name = json.at("name").get<std::string>();
        metadata.content_path = json.at("content_path").get<std::string>();
        metadata.source_path = json.value("source_path", std::string{});

        if (!ParseStatus(json.value("status", std::string{"ready"}), metadata.status))
        {
            if (diagnostic) *diagnostic = "unknown content metadata status";
            return false;
        }
        if (!ParseVisibility(json.value("visibility", std::string{"visible"}), metadata.visibility))
        {
            if (diagnostic) *diagnostic = "unknown content metadata visibility";
            return false;
        }

        metadata.references.clear();
        for (const Json& reference_json : json.value("references", Json::array()))
        {
            metadata.references.push_back({reference_json.at("slot").get<std::string>(),
                                           ContentID(reference_json.at("target").get<std::string>())});
        }

        metadata.products.clear();
        for (const Json& product_json : json.value("products", Json::array()))
        {
            ContentProduct product;
            product.type = static_cast<ArchiveProductType>(product_json.at("type").get<int>());
            const auto parsed_hash = ContentHash::FromHex(product_json.at("hash").get<std::string>());
            if (!parsed_hash) return false;
            product.hash = *parsed_hash;
            metadata.products.push_back(product);
        }
        return metadata.id.IsValid() && !metadata.content_path.empty();
    }
    catch (const std::exception& exception)
    {
        if (diagnostic) *diagnostic = exception.what();
        return false;
    }
}

bool IsSafeRelativePath(const std::filesystem::path& path)
{
    if (path.empty() || path.is_absolute()) return false;
    for (const auto& component : path)
    {
        if (component == "..") return false;
    }
    return true;
}

} // namespace

ContentID MakeContentID(const std::string& type_name, const std::string& logical_key)
{
    const std::string seed = type_name + "\n" + logical_key;
    std::ostringstream stream;
    stream << "cid-" << std::hex << std::setfill('0') << std::setw(16) << StableHash(seed, 0)
           << std::setw(16) << StableHash(seed, 0x9e3779b97f4a7c15ull);
    return ContentID(stream.str());
}

std::filesystem::path MetadataPath(const std::filesystem::path& content_root,
                                    const std::string& content_path)
{
    std::filesystem::path relative = std::filesystem::path(content_path).lexically_normal();
    if (!IsSafeRelativePath(relative)) return {};
    if (relative.extension() != ".kpmeta") relative += ".kpmeta";
    return content_root / relative;
}

bool WriteContentMetadata(const std::filesystem::path& content_root,
                          const ContentMetadata& metadata,
                          std::string* diagnostic)
{
    const std::filesystem::path path = MetadataPath(content_root, metadata.content_path);
    if (path.empty())
    {
        if (diagnostic) *diagnostic = "content metadata path is not safe";
        return false;
    }

    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    if (error)
    {
        if (diagnostic) *diagnostic = error.message();
        return false;
    }

    const std::filesystem::path temporary_path = path.string() + ".tmp";
    {
        std::ofstream output(temporary_path, std::ios::binary | std::ios::trunc);
        if (!output)
        {
            if (diagnostic) *diagnostic = "could not open metadata temporary file";
            return false;
        }
        output << ToJson(metadata).dump(2) << '\n';
        if (!output)
        {
            if (diagnostic) *diagnostic = "could not write metadata temporary file";
            return false;
        }
    }

    std::filesystem::remove(path, error);
    error.clear();
    std::filesystem::rename(temporary_path, path, error);
    if (error)
    {
        if (diagnostic) *diagnostic = error.message();
        std::filesystem::remove(temporary_path, error);
        return false;
    }
    return true;
}

bool ReadContentMetadata(const std::filesystem::path& metadata_path,
                         ContentMetadata& metadata,
                         std::string* diagnostic)
{
    std::ifstream input(metadata_path, std::ios::binary);
    if (!input)
    {
        if (diagnostic) *diagnostic = "could not open metadata file";
        return false;
    }

    try
    {
        const Json json = Json::parse(input);
        return FromJson(json, metadata, diagnostic);
    }
    catch (const std::exception& exception)
    {
        if (diagnostic) *diagnostic = exception.what();
        return false;
    }
}

ContentRegistry::ContentRegistry(std::filesystem::path content_root)
    : content_root_(std::move(content_root))
{
}

ContentRegistrySnapshot ContentRegistry::Capture() const
{
    ContentRegistrySnapshot snapshot;
    std::error_code error;
    if (!std::filesystem::exists(content_root_, error)) return snapshot;

    std::filesystem::recursive_directory_iterator iterator(content_root_, error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end)
    {
        const auto current = iterator->path();
        if (iterator->is_directory(error) && current.filename() == ".archive")
        {
            iterator.disable_recursion_pending();
        }
        else if (!error && iterator->is_regular_file(error) && current.extension() == ".kpmeta")
        {
            ContentMetadata metadata;
            std::string diagnostic;
            if (ReadContentMetadata(current, metadata, &diagnostic)) snapshot.records.push_back(std::move(metadata));
            else snapshot.diagnostics.push_back(current.string() + ": " + diagnostic);
        }
        iterator.increment(error);
    }

    std::sort(snapshot.records.begin(), snapshot.records.end(), [](const ContentMetadata& lhs, const ContentMetadata& rhs) {
        return lhs.content_path < rhs.content_path;
    });
    if (error) snapshot.diagnostics.push_back(content_root_.string() + ": " + error.message());
    return snapshot;
}

} // namespace kpengine::asset
