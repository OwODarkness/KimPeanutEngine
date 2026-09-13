#ifndef KPENGINE_RUNTIME_ASSET_DETAIL_ASSET_CATALOG_BUILDER_H
#define KPENGINE_RUNTIME_ASSET_DETAIL_ASSET_CATALOG_BUILDER_H

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "asset/asset_catalog.h"
#include "asset/common.h"
#include "asset/model_archive.h"

// Asset-private graph assembly. The provider owns archive reads and the
// bounded manager copy; everything below this header is a pure function of
// copied values so joins, aliases, relations, and failures can be tested
// without SQLite, the AssetManager singleton, or a lock.
namespace kpengine::asset::detail
{
    // Plain copied manager state. It holds no payload, cache pointer, registry
    // descriptor, callback, or lock, so it may outlive the capture phase.
    struct LiveCatalogRecord
    {
        AssetID id;
        AssetType type{AssetType::Undefined};
        std::string type_name;
        std::string name;
        std::string path;
        // True only when the manager path index still resolves to this exact id.
        bool path_indexed{};
        std::vector<AssetID> dependencies;
        std::vector<AssetID> owned_children;
    };

    // Registry name for a type that no copied live record exposes. Sorted by
    // type and unique; the first entry for a type wins.
    struct TypeNameRecord
    {
        AssetType type{AssetType::Undefined};
        std::string name;

        bool operator<(const TypeNameRecord &rhs) const noexcept
        {
            return type < rhs.type;
        }
    };

    struct AssetCatalogBuildLimits
    {
        std::size_t max_nodes{250'000};
        std::size_t max_edges{1'000'000};
        std::size_t max_aliases_per_node{64};
        std::size_t max_diagnostics{1'024};
    };

    struct AssetCatalogBuildInput
    {
        std::uint64_t revision{};
        // ProductRecord::relative_path is resolved against this root.
        std::filesystem::path archive_root;
        // Asset root used to derive project-relative logical paths.
        std::filesystem::path asset_root;
        std::vector<TypeNameRecord> type_names;
        std::vector<LiveCatalogRecord> live;
        // Set when the bounded copy declined to publish a subset of unordered
        // cache entries. The live portion is then dropped entirely and this
        // text becomes one CaptureLimitExceeded diagnostic.
        std::string live_limit_diagnostic;
        // Null when the archive read failed. The live graph is still published
        // and `archive_diagnostic` explains why.
        const ModelArchiveCatalogSnapshot *archive{};
        std::string archive_diagnostic;
        AssetCatalogDiagnosticCode archive_diagnostic_code{
            AssetCatalogDiagnosticCode::ArchiveUnavailable};
        AssetCatalogDiagnosticSeverity archive_diagnostic_severity{
            AssetCatalogDiagnosticSeverity::Error};
        AssetCatalogBuildLimits limits;
    };

    // Returns a canonical, validated snapshot. A provider programming error
    // yields a minimal valid Partial snapshot with CatalogAssemblyFailed rather
    // than a malformed graph or an exception.
    AssetCatalogSnapshot BuildAssetCatalog(const AssetCatalogBuildInput &input);
}

#endif
