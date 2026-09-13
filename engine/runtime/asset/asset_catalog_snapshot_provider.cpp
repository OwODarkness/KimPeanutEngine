#include "asset_catalog_snapshot_provider.h"

#include <array>
#include <mutex>
#include <string>
#include <utility>

#include "asset/detail/asset_catalog_builder.h"
#include "asset_manager.h"
#include "config/path.h"

namespace kpengine::asset
{
    namespace
    {
        // Archive product types have built-in Asset counterparts, so their
        // registry names make archive-only nodes readable without a switch.
        constexpr std::array<AssetType, 3> kArchiveMappedTypes{
            AssetType::KPAT_Model, AssetType::KPAT_Material, AssetType::KPAT_Texture};

        std::string ArchiveDiagnosticMessage(const std::filesystem::path &database_path,
                                             const ModelArchiveError &error)
        {
            return "archive catalog read failed for '" + database_path.generic_string() +
                   "': " + error.what();
        }
    }

    AssetCatalogSnapshotProvider::AssetCatalogSnapshotProvider(
        AssetManager &assets, AssetCatalogProviderConfig config)
        : assets_(assets), config_(std::move(config))
    {
    }

    void AssetCatalogSnapshotProvider::CaptureLive(
        std::vector<detail::LiveCatalogRecord> &records,
        std::vector<detail::TypeNameRecord> &type_names, bool &limit_exceeded) const
    {
        limit_exceeded = false;
        std::lock_guard<std::recursive_mutex> lock(assets_.state_mutex_);

        // Archive-only nodes have no live record to take a name from, so the
        // registry names are resolved here while the registry is protected.
        for (const AssetType type : kArchiveMappedTypes)
        {
            const AssetTypeDescriptor *const descriptor = assets_.type_registry_.FindByType(type);
            if (descriptor != nullptr && !descriptor->name.empty())
            {
                type_names.push_back({type, descriptor->name});
            }
        }

        std::size_t live_count = 0;
        for (const auto &[type, cache] : assets_.caches_)
        {
            (void)type;
            for (std::size_t index = 0; index < cache.assets.size(); ++index)
            {
                const Asset *const asset = cache.assets[index].get();
                if (asset == nullptr)
                {
                    continue;
                }
                const AssetID id = asset->GetID();
                if (!cache.handles.IsHandleValid(AssetHandle(id.id, id.generation)))
                {
                    continue;
                }
                ++live_count;
            }
        }
        if (live_count > config_.limits.max_live_assets)
        {
            limit_exceeded = true;
            return;
        }

        records.reserve(live_count);
        for (const auto &[type, cache] : assets_.caches_)
        {
            (void)type;
            for (std::size_t index = 0; index < cache.assets.size(); ++index)
            {
                const Asset *const asset = cache.assets[index].get();
                if (asset == nullptr)
                {
                    continue;
                }
                const AssetID id = asset->GetID();
                if (!cache.handles.IsHandleValid(AssetHandle(id.id, id.generation)))
                {
                    continue;
                }

                detail::LiveCatalogRecord record;
                record.id = id;
                record.type = id.type;
                if (const AssetTypeDescriptor *const descriptor =
                        assets_.type_registry_.FindByType(id.type))
                {
                    record.type_name = descriptor->name;
                }
                // Copied values only: no payload, cache entry, callback, or
                // registry descriptor crosses this point.
                record.name = asset->GetName();
                record.path = asset->GetPath();
                record.dependencies = asset->GetDependencies();
                record.owned_children = asset->GetOwnedChildren();
                if (!record.path.empty())
                {
                    const auto found = cache.path_index.find(AssetManager::Key(record.path));
                    record.path_indexed =
                        found != cache.path_index.end() && found->second == id;
                }
                records.push_back(std::move(record));
            }
        }
    }

    AssetCatalogSnapshot AssetCatalogSnapshotProvider::CaptureAssetCatalog()
    {
        const std::uint64_t revision =
            revision_counter_.fetch_add(1, std::memory_order_relaxed) + 1;

        detail::AssetCatalogBuildInput input;
        input.revision = revision;
        input.limits.max_nodes = config_.limits.max_nodes;
        input.limits.max_edges = config_.limits.max_edges;
        input.limits.max_aliases_per_node = config_.limits.max_aliases_per_node;
        input.limits.max_diagnostics = config_.limits.max_diagnostics;
        input.asset_root = std::filesystem::path(GetAssetDirectory());

        const std::filesystem::path database_path =
            config_.database_path.empty()
                ? input.asset_root / ".archive" / "archive.sqlite3"
                : config_.database_path;
        input.archive_root = database_path.parent_path();

        // Phase 1: one read-only connection and one read transaction, taken
        // without any Asset lock and closed before the live copy begins.
        ModelArchiveCatalogSnapshot archive;
        bool archive_available = false;
        try
        {
            ModelArchiveDatabase database(database_path, config_.busy_timeout_ms,
                                          ModelArchiveOpenMode::ReadOnly);
            archive = database.ReadCatalog(config_.limits.archive);
            archive_available = true;
        }
        catch (const ModelArchiveError &error)
        {
            // A configured limit is not archive corruption, and saying so keeps
            // the diagnostic actionable.
            input.archive_diagnostic_code =
                error.Code() == ModelArchiveErrorCode::InvalidArgument
                    ? AssetCatalogDiagnosticCode::CaptureLimitExceeded
                    : AssetCatalogDiagnosticCode::ArchiveUnavailable;
            input.archive_diagnostic_severity = AssetCatalogDiagnosticSeverity::Error;
            input.archive_diagnostic = ArchiveDiagnosticMessage(database_path, error);
        }
        if (archive_available)
        {
            input.archive = &archive;
        }

        // Phase 2: the bounded live copy. The lock is released here and is
        // never nested with the archive transaction above.
        bool live_limit_exceeded = false;
        CaptureLive(input.live, input.type_names, live_limit_exceeded);
        if (live_limit_exceeded)
        {
            input.live.clear();
            input.live_limit_diagnostic =
                "the live asset graph holds more than " +
                std::to_string(config_.limits.max_live_assets) +
                " assets, so no live catalogue was published";
        }

        return detail::BuildAssetCatalog(input);
    }
}
