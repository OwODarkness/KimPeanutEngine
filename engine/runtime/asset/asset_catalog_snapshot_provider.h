#ifndef KPENGINE_RUNTIME_ASSET_ASSET_CATALOG_SNAPSHOT_PROVIDER_H
#define KPENGINE_RUNTIME_ASSET_ASSET_CATALOG_SNAPSHOT_PROVIDER_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <vector>

#include "asset_catalog.h"
#include "model_archive.h"

namespace kpengine::asset
{
    class AssetManager;

    namespace detail
    {
        struct LiveCatalogRecord;
        struct TypeNameRecord;
    }

    struct AssetCatalogCaptureLimits
    {
        ModelArchiveCatalogReadLimits archive;
        std::size_t max_live_assets{250'000};
        std::size_t max_nodes{250'000};
        std::size_t max_edges{1'000'000};
        std::size_t max_aliases_per_node{64};
        std::size_t max_diagnostics{1'024};
    };

    struct AssetCatalogProviderConfig
    {
        // Empty selects the content archive, with the legacy asset archive as fallback.
        std::filesystem::path database_path;
        std::int32_t busy_timeout_ms{50};
        AssetCatalogCaptureLimits limits;
    };

    // Asset-owned implementation of the catalog provider boundary. It borrows
    // the process-lifetime manager, owns its archive configuration and revision
    // counter, and opens one short read-only archive connection per capture.
    class AssetCatalogSnapshotProvider final : public IAssetCatalogSnapshotSource
    {
    public:
        AssetCatalogSnapshotProvider(AssetManager &assets, AssetCatalogProviderConfig config);

        // Reads the archive without an Asset lock, then copies one coherent
        // live epoch under `state_mutex_` and releases it before any graph
        // assembly. The result refers to no wrapper, descriptor, payload,
        // statement, or lock.
        AssetCatalogSnapshot CaptureAssetCatalog() override;

    private:
        // The only code allowed to read manager storage. A limit breach drops
        // the whole live portion rather than publishing an arbitrary subset of
        // unordered cache entries.
        void CaptureLive(std::vector<detail::LiveCatalogRecord> &records,
                         std::vector<detail::TypeNameRecord> &type_names,
                         bool &limit_exceeded) const;

        AssetManager &assets_;
        AssetCatalogProviderConfig config_;
        // Non-zero, process-local, and allocated before either read phase so
        // every attempt is distinguishable even when content is not.
        std::atomic<std::uint64_t> revision_counter_{0};
    };
}

#endif
