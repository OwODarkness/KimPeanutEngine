#ifndef KPENGINE_RUNTIME_ASSET_DETAIL_CONTENT_CATALOG_BUILDER_H
#define KPENGINE_RUNTIME_ASSET_DETAIL_CONTENT_CATALOG_BUILDER_H

#include <cstddef>
#include <cstdint>
#include <filesystem>

#include "asset/asset_catalog.h"
#include "asset/content_metadata.h"

namespace kpengine::asset::detail
{
    struct ContentCatalogBuildLimits
    {
        std::size_t max_nodes{250'000};
        std::size_t max_edges{1'000'000};
        std::size_t max_diagnostics{1'024};
    };

    struct ContentCatalogBuildInput
    {
        std::uint64_t revision{};
        std::filesystem::path archive_root;
        const ContentRegistrySnapshot *registry{};
        ContentCatalogBuildLimits limits;
    };

    // Converts copied metadata into the same immutable catalog contract used by
    // the legacy archive/live builder. It never reads metadata files or Asset
    // state; the provider owns those capture operations.
    AssetCatalogSnapshot BuildContentAssetCatalog(const ContentCatalogBuildInput &input);
}

#endif // KPENGINE_RUNTIME_ASSET_DETAIL_CONTENT_CATALOG_BUILDER_H
