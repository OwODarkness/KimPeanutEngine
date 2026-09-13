#include "asset_catalog.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "utility.h"

namespace kpengine::asset
{
    namespace
    {
        bool MaterialOverrideLess(const AssetCatalogMaterialOverride &lhs,
                                  const AssetCatalogMaterialOverride &rhs)
        {
            if (lhs.slot != rhs.slot)
            {
                return lhs.slot < rhs.slot;
            }
            return lhs.authored_path < rhs.authored_path;
        }

        // Total order over provenance entries. Entry sub-vectors are normalized
        // before entries are ordered, so byte-identical entries are the only
        // comparison ties and reordering them cannot change the snapshot.
        bool ProvenanceLess(const AssetCatalogProvenance &lhs,
                            const AssetCatalogProvenance &rhs)
        {
            if (lhs.source_path != rhs.source_path)
            {
                return lhs.source_path < rhs.source_path;
            }
            if (lhs.source_display_name != rhs.source_display_name)
            {
                return lhs.source_display_name < rhs.source_display_name;
            }
            if (lhs.source_dependency_paths != rhs.source_dependency_paths)
            {
                return lhs.source_dependency_paths < rhs.source_dependency_paths;
            }
            const std::size_t shared = std::min(lhs.material_overrides.size(),
                                                rhs.material_overrides.size());
            for (std::size_t index = 0; index < shared; ++index)
            {
                const AssetCatalogMaterialOverride &left = lhs.material_overrides[index];
                const AssetCatalogMaterialOverride &right = rhs.material_overrides[index];
                if (left.slot != right.slot)
                {
                    return left.slot < right.slot;
                }
                if (left.authored_path != right.authored_path)
                {
                    return left.authored_path < right.authored_path;
                }
            }
            return lhs.material_overrides.size() < rhs.material_overrides.size();
        }

        bool EdgeLess(const AssetCatalogSnapshot &snapshot,
                      const AssetCatalogEdge &lhs, const AssetCatalogEdge &rhs)
        {
            if (lhs.from.value != rhs.from.value)
            {
                return lhs.from.value < rhs.from.value;
            }
            const auto lhs_relation = static_cast<std::uint8_t>(lhs.relation);
            const auto rhs_relation = static_cast<std::uint8_t>(rhs.relation);
            if (lhs_relation != rhs_relation)
            {
                return lhs_relation < rhs_relation;
            }
            if (lhs.ordinal != rhs.ordinal)
            {
                return lhs.ordinal < rhs.ordinal;
            }
            return snapshot.nodes[lhs.to.value].stable_key <
                   snapshot.nodes[rhs.to.value].stable_key;
        }

        bool DiagnosticLess(const AssetCatalogDiagnostic &lhs,
                            const AssetCatalogDiagnostic &rhs)
        {
            const auto lhs_severity = static_cast<std::uint8_t>(lhs.severity);
            const auto rhs_severity = static_cast<std::uint8_t>(rhs.severity);
            if (lhs_severity != rhs_severity)
            {
                return lhs_severity < rhs_severity;
            }
            const auto lhs_code = static_cast<std::uint8_t>(lhs.code);
            const auto rhs_code = static_cast<std::uint8_t>(rhs.code);
            if (lhs_code != rhs_code)
            {
                return lhs_code < rhs_code;
            }
            if (lhs.related_stable_key != rhs.related_stable_key)
            {
                return lhs.related_stable_key < rhs.related_stable_key;
            }
            return lhs.message < rhs.message;
        }

        std::string_view AvailabilityName(AssetCatalogAvailability availability)
        {
            switch (availability)
            {
            case AssetCatalogAvailability::ArchiveOnly:
                return "ArchiveOnly";
            case AssetCatalogAvailability::LoadedArchiveProduct:
                return "LoadedArchiveProduct";
            case AssetCatalogAvailability::RuntimeOnly:
                return "RuntimeOnly";
            case AssetCatalogAvailability::Missing:
                return "Missing";
            }
            return "Unknown";
        }

        bool IsArchiveAvailability(AssetCatalogAvailability availability)
        {
            return availability == AssetCatalogAvailability::ArchiveOnly ||
                   availability == AssetCatalogAvailability::LoadedArchiveProduct;
        }

        std::string FormatDecimal(std::uint64_t value)
        {
            return std::to_string(value);
        }

        std::string FormatLowerHex(std::uint64_t value)
        {
            static constexpr char kDigits[] = "0123456789abcdef";
            std::string result(16, '0');
            for (int index = 15; index >= 0; --index)
            {
                result[static_cast<std::size_t>(index)] =
                    kDigits[value & 0xfu];
                value >>= 4;
            }
            return result;
        }

        std::string_view RelationToken(AssetCatalogRelation relation)
        {
            switch (relation)
            {
            case AssetCatalogRelation::Dependency:
                return "dependency";
            case AssetCatalogRelation::OwnedChild:
                return "owned-child";
            }
            return "dependency";
        }

        bool AliasesAreCanonical(const std::vector<std::string> &aliases)
        {
            for (std::size_t index = 0; index < aliases.size(); ++index)
            {
                if (aliases[index].empty())
                {
                    return false;
                }
                if (index > 0 && !(aliases[index - 1] < aliases[index]))
                {
                    return false;
                }
            }
            return true;
        }

        bool DependencyPathsAreCanonical(const std::vector<std::string> &paths)
        {
            for (std::size_t index = 0; index < paths.size(); ++index)
            {
                if (paths[index].empty())
                {
                    return false;
                }
                if (index > 0 && paths[index] < paths[index - 1])
                {
                    return false;
                }
            }
            return true;
        }

        bool MaterialOverridesAreCanonical(
            const std::vector<AssetCatalogMaterialOverride> &overrides)
        {
            for (std::size_t index = 0; index < overrides.size(); ++index)
            {
                if (overrides[index].authored_path.empty())
                {
                    return false;
                }
                if (index > 0 && overrides[index].slot == overrides[index - 1].slot)
                {
                    return false;
                }
                if (index > 0 &&
                    MaterialOverrideLess(overrides[index], overrides[index - 1]))
                {
                    return false;
                }
            }
            return true;
        }

        bool ProvenanceIsCanonical(const std::vector<AssetCatalogProvenance> &provenance)
        {
            for (std::size_t index = 0; index < provenance.size(); ++index)
            {
                const AssetCatalogProvenance &entry = provenance[index];
                if (entry.source_path.empty())
                {
                    return false;
                }
                if (!DependencyPathsAreCanonical(entry.source_dependency_paths) ||
                    !MaterialOverridesAreCanonical(entry.material_overrides))
                {
                    return false;
                }
                if (index > 0 && ProvenanceLess(entry, provenance[index - 1]))
                {
                    return false;
                }
            }
            return true;
        }

        // Structural preflight. It rejects everything that makes the graph
        // ambiguous or uninterpretable, and it deliberately ignores ordering so
        // that CanonicalizeAssetCatalogSnapshot can accept unsorted input.
        bool PreflightAssetCatalogSnapshot(const AssetCatalogSnapshot &snapshot,
                                           std::string &diagnostic)
        {
            const std::size_t node_count = snapshot.nodes.size();
            for (std::size_t index = 0; index < node_count; ++index)
            {
                const AssetCatalogNode &node = snapshot.nodes[index];
                if (!node.id.IsValid() || node.id.value != index)
                {
                    diagnostic = "catalog node " + std::to_string(index) +
                                 " must carry its dense index as its node id";
                    return false;
                }
                if (node.stable_key.empty())
                {
                    diagnostic = "catalog node " + std::to_string(index) +
                                 " has an empty stable key";
                    return false;
                }
                for (const AssetCatalogProvenance &entry : node.provenance)
                {
                    for (std::size_t slot_index = 1;
                         slot_index < entry.material_overrides.size(); ++slot_index)
                    {
                        if (entry.material_overrides[slot_index].slot ==
                            entry.material_overrides[slot_index - 1].slot)
                        {
                            diagnostic = "catalog node " + std::to_string(index) +
                                         " has two material overrides for slot " +
                                         std::to_string(
                                             entry.material_overrides[slot_index].slot);
                            return false;
                        }
                    }
                }
            }

            std::vector<std::string_view> sorted_keys;
            sorted_keys.reserve(node_count);
            for (const AssetCatalogNode &node : snapshot.nodes)
            {
                sorted_keys.push_back(node.stable_key);
            }
            std::sort(sorted_keys.begin(), sorted_keys.end());
            const auto duplicate = std::adjacent_find(sorted_keys.begin(),
                                                      sorted_keys.end());
            if (duplicate != sorted_keys.end())
            {
                diagnostic = "duplicate catalog stable key: " + std::string(*duplicate);
                return false;
            }

            for (const AssetCatalogEdge &edge : snapshot.edges)
            {
                if (edge.from.value >= node_count || edge.to.value >= node_count)
                {
                    diagnostic = "catalog edge endpoint is not a node in this snapshot";
                    return false;
                }
                if (!edge.from.IsValid() || !edge.to.IsValid())
                {
                    diagnostic = "catalog edge endpoint is an invalid node id";
                    return false;
                }
            }

            struct EdgeOrdinal
            {
                std::uint32_t from{};
                std::uint8_t relation{};
                std::uint32_t ordinal{};

                bool operator<(const EdgeOrdinal &rhs) const noexcept
                {
                    if (from != rhs.from)
                    {
                        return from < rhs.from;
                    }
                    if (relation != rhs.relation)
                    {
                        return relation < rhs.relation;
                    }
                    return ordinal < rhs.ordinal;
                }

                bool operator==(const EdgeOrdinal &rhs) const noexcept
                {
                    return from == rhs.from && relation == rhs.relation &&
                           ordinal == rhs.ordinal;
                }
            };

            std::vector<EdgeOrdinal> ordinals;
            ordinals.reserve(snapshot.edges.size());
            for (const AssetCatalogEdge &edge : snapshot.edges)
            {
                ordinals.push_back(EdgeOrdinal{edge.from.value,
                                               static_cast<std::uint8_t>(edge.relation),
                                               edge.ordinal});
            }
            std::sort(ordinals.begin(), ordinals.end());
            const auto duplicate_ordinal = std::adjacent_find(ordinals.begin(),
                                                             ordinals.end());
            if (duplicate_ordinal != ordinals.end())
            {
                diagnostic = "catalog has two edges for (from, relation, ordinal) " +
                             std::to_string(duplicate_ordinal->from) + "/" +
                             std::to_string(duplicate_ordinal->relation) + "/" +
                             std::to_string(duplicate_ordinal->ordinal);
                return false;
            }

            for (const AssetCatalogDiagnostic &entry : snapshot.diagnostics)
            {
                if (entry.related_stable_key.empty())
                {
                    continue;
                }
                if (!std::binary_search(sorted_keys.begin(), sorted_keys.end(),
                                        std::string_view(entry.related_stable_key)))
                {
                    diagnostic = "catalog diagnostic references unknown stable key: " +
                                 entry.related_stable_key;
                    return false;
                }
            }
            return true;
        }

        bool NodeIsValid(const AssetCatalogSnapshot &snapshot,
                         const AssetCatalogNode &node, std::string &diagnostic)
        {
            if (node.stable_key.compare(0, kAssetCatalogKeyPrefix.size(),
                                        kAssetCatalogKeyPrefix) != 0)
            {
                diagnostic = "catalog node stable key is not in the " +
                             std::string(kAssetCatalogKeyPrefix) + " namespace: " +
                             node.stable_key;
                return false;
            }

            if (node.kind == AssetCatalogNodeKind::MissingReference)
            {
                if (node.availability != AssetCatalogAvailability::Missing)
                {
                    diagnostic = "missing-reference node " + node.stable_key +
                                 " must use Missing availability";
                    return false;
                }
            }
            else if (node.availability == AssetCatalogAvailability::Missing)
            {
                diagnostic = "catalog node " + node.stable_key +
                             " uses Missing availability without MissingReference kind";
                return false;
            }
            else
            {
                if (node.type == AssetType::Undefined)
                {
                    diagnostic = "catalog node " + node.stable_key +
                                 " requires a concrete asset type";
                    return false;
                }
                if (node.type_name.empty())
                {
                    diagnostic = "catalog node " + node.stable_key +
                                 " requires a readable type name";
                    return false;
                }
            }

            switch (node.availability)
            {
            case AssetCatalogAvailability::LoadedArchiveProduct:
                if (node.dependency_coverage !=
                    AssetCatalogDependencyCoverage::Complete)
                {
                    diagnostic = "loaded node " + node.stable_key +
                                 " requires complete dependency coverage";
                    return false;
                }
                break;
            case AssetCatalogAvailability::ArchiveOnly:
            case AssetCatalogAvailability::Missing:
                if (node.dependency_coverage !=
                    AssetCatalogDependencyCoverage::Unknown)
                {
                    diagnostic = "node " + node.stable_key + " with " +
                                 std::string(AvailabilityName(node.availability)) +
                                 " availability requires unknown dependency coverage";
                    return false;
                }
                break;
            case AssetCatalogAvailability::RuntimeOnly:
                break;
            }

            if (IsArchiveAvailability(node.availability))
            {
                if (!node.archive_product_type.has_value())
                {
                    diagnostic = "archive node " + node.stable_key +
                                 " requires an archive product type";
                    return false;
                }
                if (node.product_path.empty())
                {
                    diagnostic = "archive node " + node.stable_key +
                                 " requires a canonical product path";
                    return false;
                }
                if (node.availability == AssetCatalogAvailability::ArchiveOnly)
                {
                    if (!node.content_hash.has_value())
                    {
                        diagnostic = "archive-only node " + node.stable_key +
                                     " requires a content hash";
                        return false;
                    }
                    if (node.packed_runtime_asset_id.has_value())
                    {
                        diagnostic = "archive-only node " + node.stable_key +
                                     " must not carry a runtime asset id";
                        return false;
                    }
                }
            }
            else
            {
                if (node.archive_product_type.has_value())
                {
                    diagnostic = "node " + node.stable_key + " with " +
                                 std::string(AvailabilityName(node.availability)) +
                                 " availability must not carry an archive product type";
                    return false;
                }
                if (node.availability == AssetCatalogAvailability::Missing)
                {
                    if (node.content_hash.has_value())
                    {
                        diagnostic = "missing node " + node.stable_key +
                                     " must not carry a content hash";
                        return false;
                    }
                    if (node.packed_runtime_asset_id.has_value())
                    {
                        diagnostic = "missing node " + node.stable_key +
                                     " must not carry a runtime asset id";
                        return false;
                    }
                }
            }

            if (!AliasesAreCanonical(node.aliases))
            {
                diagnostic = "catalog node " + node.stable_key +
                             " has empty, unsorted, or duplicate aliases";
                return false;
            }
            if (!ProvenanceIsCanonical(node.provenance))
            {
                diagnostic = "catalog node " + node.stable_key +
                             " has unsorted or malformed import provenance";
                return false;
            }

            if (node.kind == AssetCatalogNodeKind::MissingReference)
            {
                const bool has_unresolved = std::any_of(
                    snapshot.diagnostics.begin(), snapshot.diagnostics.end(),
                    [&node](const AssetCatalogDiagnostic &entry)
                    {
                        return entry.code == AssetCatalogDiagnosticCode::UnresolvedDependency &&
                               entry.related_stable_key == node.stable_key;
                    });
                if (!has_unresolved)
                {
                    diagnostic = "missing-reference node " + node.stable_key +
                                 " requires an unresolved-dependency diagnostic";
                    return false;
                }
            }
            return true;
        }

        bool NodesAreCanonical(const AssetCatalogSnapshot &snapshot)
        {
            for (std::size_t index = 1; index < snapshot.nodes.size(); ++index)
            {
                if (!(snapshot.nodes[index - 1].stable_key <
                      snapshot.nodes[index].stable_key))
                {
                    return false;
                }
            }
            return true;
        }

        bool EdgesAreCanonical(const AssetCatalogSnapshot &snapshot)
        {
            for (std::size_t index = 1; index < snapshot.edges.size(); ++index)
            {
                if (EdgeLess(snapshot, snapshot.edges[index],
                             snapshot.edges[index - 1]))
                {
                    return false;
                }
            }
            return true;
        }

        bool DiagnosticsAreCanonical(const AssetCatalogSnapshot &snapshot)
        {
            for (std::size_t index = 1; index < snapshot.diagnostics.size(); ++index)
            {
                if (DiagnosticLess(snapshot.diagnostics[index],
                                   snapshot.diagnostics[index - 1]))
                {
                    return false;
                }
            }
            return true;
        }

        // Missing targets are leaves: they stand in for an asset that does not
        // exist, so they can neither own dependencies nor stand alone.
        bool MissingReferencesAreLeaves(const AssetCatalogSnapshot &snapshot,
                                        std::string &diagnostic)
        {
            std::vector<bool> has_incoming(snapshot.nodes.size(), false);
            for (const AssetCatalogEdge &edge : snapshot.edges)
            {
                if (edge.from.value == edge.to.value &&
                    edge.relation == AssetCatalogRelation::OwnedChild)
                {
                    diagnostic = "catalog node " +
                                 snapshot.nodes[edge.from.value].stable_key +
                                 " is its own owned child";
                    return false;
                }
                has_incoming[edge.to.value] = true;
                if (snapshot.nodes[edge.from.value].kind ==
                        AssetCatalogNodeKind::MissingReference)
                {
                    diagnostic = "missing-reference node " +
                                 snapshot.nodes[edge.from.value].stable_key +
                                 " must not be the source of an edge";
                    return false;
                }
            }
            for (std::size_t index = 0; index < snapshot.nodes.size(); ++index)
            {
                if (snapshot.nodes[index].kind == AssetCatalogNodeKind::MissingReference &&
                    !has_incoming[index])
                {
                    diagnostic = "missing-reference node " +
                                 snapshot.nodes[index].stable_key +
                                 " has no referencing edge";
                    return false;
                }
            }
            return true;
        }

        void NormalizeAliases(std::vector<std::string> &aliases)
        {
            aliases.erase(std::remove_if(aliases.begin(), aliases.end(),
                                         [](const std::string &alias)
                                         { return alias.empty(); }),
                          aliases.end());
            std::sort(aliases.begin(), aliases.end());
            aliases.erase(std::unique(aliases.begin(), aliases.end()), aliases.end());
        }

        void NormalizeProvenance(std::vector<AssetCatalogProvenance> &provenance)
        {
            for (AssetCatalogProvenance &entry : provenance)
            {
                std::vector<std::string> &paths = entry.source_dependency_paths;
                paths.erase(std::remove_if(paths.begin(), paths.end(),
                                           [](const std::string &path)
                                           { return path.empty(); }),
                            paths.end());
                std::sort(paths.begin(), paths.end());

                std::sort(entry.material_overrides.begin(),
                          entry.material_overrides.end(), MaterialOverrideLess);
            }
            std::sort(provenance.begin(), provenance.end(), ProvenanceLess);
        }
    }

    std::string MakeArchiveProductCatalogKey(ArchiveProductType type,
                                             const ContentHash &hash)
    {
        std::string key(kAssetCatalogKeyPrefix);
        key += "product/";
        key += FormatDecimal(static_cast<std::uint8_t>(type));
        key += '/';
        key += hash.ToHex();
        return key;
    }

    std::string MakeRuntimePathCatalogKey(AssetType type,
                                          std::string_view canonical_path_key)
    {
        std::string key(kAssetCatalogKeyPrefix);
        key += "path/";
        key += FormatDecimal(static_cast<std::uint16_t>(type));
        key += '/';
        // Idempotent for callers that already canonicalized through
        // AssetManager::Key, and self-correcting for callers that did not.
        key += CanonicalAssetPathKey(std::string(canonical_path_key));
        return key;
    }

    std::string MakeRuntimeIdentityCatalogKey(AssetID id)
    {
        std::string key(kAssetCatalogKeyPrefix);
        key += "runtime/";
        key += FormatLowerHex(id.Pack());
        return key;
    }

    std::string MakeMissingReferenceCatalogKey(std::string_view owner_key,
                                              AssetCatalogRelation relation,
                                              std::uint32_t ordinal,
                                              AssetType expected_type)
    {
        std::string key(kAssetCatalogKeyPrefix);
        key += "missing/";
        key += Sha256(owner_key).ToHex();
        key += '/';
        key += RelationToken(relation);
        key += '/';
        key += FormatDecimal(ordinal);
        key += '/';
        key += FormatDecimal(static_cast<std::uint16_t>(expected_type));
        return key;
    }

    bool ValidateAssetCatalogSnapshot(const AssetCatalogSnapshot &snapshot,
                                      std::string &diagnostic)
    {
        diagnostic.clear();
        if (!PreflightAssetCatalogSnapshot(snapshot, diagnostic))
        {
            return false;
        }
        if (!NodesAreCanonical(snapshot))
        {
            diagnostic = "catalog nodes are not sorted by stable key ascending";
            return false;
        }
        for (const AssetCatalogNode &node : snapshot.nodes)
        {
            if (!NodeIsValid(snapshot, node, diagnostic))
            {
                return false;
            }
        }
        if (!EdgesAreCanonical(snapshot))
        {
            diagnostic = "catalog edges are not in canonical order";
            return false;
        }
        if (!DiagnosticsAreCanonical(snapshot))
        {
            diagnostic = "catalog diagnostics are not in canonical order";
            return false;
        }
        if (!MissingReferencesAreLeaves(snapshot, diagnostic))
        {
            return false;
        }
        if (snapshot.status == AssetCatalogSnapshotStatus::Complete)
        {
            const auto error = std::find_if(
                snapshot.diagnostics.begin(), snapshot.diagnostics.end(),
                [](const AssetCatalogDiagnostic &entry)
                {
                    return entry.severity == AssetCatalogDiagnosticSeverity::Error;
                });
            if (error != snapshot.diagnostics.end())
            {
                diagnostic = "complete catalog snapshot carries an error diagnostic: " +
                             error->message;
                return false;
            }
        }
        return true;
    }

    bool CanonicalizeAssetCatalogSnapshot(AssetCatalogSnapshot &snapshot,
                                          std::string &diagnostic)
    {
        diagnostic.clear();
        AssetCatalogSnapshot working = snapshot;
        if (!PreflightAssetCatalogSnapshot(working, diagnostic))
        {
            return false;
        }

        for (AssetCatalogNode &node : working.nodes)
        {
            NormalizeAliases(node.aliases);
            NormalizeProvenance(node.provenance);
        }

        std::vector<std::uint32_t> insertion_order(working.nodes.size());
        for (std::size_t index = 0; index < insertion_order.size(); ++index)
        {
            insertion_order[index] = static_cast<std::uint32_t>(index);
        }
        std::sort(insertion_order.begin(), insertion_order.end(),
                  [&working](std::uint32_t lhs, std::uint32_t rhs)
                  {
                      return working.nodes[lhs].stable_key <
                             working.nodes[rhs].stable_key;
                  });

        std::vector<std::uint32_t> old_to_new(working.nodes.size(), 0);
        std::vector<AssetCatalogNode> sorted_nodes;
        sorted_nodes.reserve(working.nodes.size());
        for (std::size_t new_index = 0; new_index < insertion_order.size(); ++new_index)
        {
            const std::uint32_t old_index = insertion_order[new_index];
            old_to_new[old_index] = static_cast<std::uint32_t>(new_index);
            sorted_nodes.push_back(std::move(working.nodes[old_index]));
        }
        for (std::size_t index = 0; index < sorted_nodes.size(); ++index)
        {
            sorted_nodes[index].id.value = static_cast<std::uint32_t>(index);
        }
        working.nodes = std::move(sorted_nodes);

        for (AssetCatalogEdge &edge : working.edges)
        {
            edge.from.value = old_to_new[edge.from.value];
            edge.to.value = old_to_new[edge.to.value];
        }
        std::sort(working.edges.begin(), working.edges.end(),
                  [&working](const AssetCatalogEdge &lhs, const AssetCatalogEdge &rhs)
                  { return EdgeLess(working, lhs, rhs); });

        std::sort(working.diagnostics.begin(), working.diagnostics.end(),
                  DiagnosticLess);

        if (!ValidateAssetCatalogSnapshot(working, diagnostic))
        {
            return false;
        }
        snapshot = std::move(working);
        return true;
    }
}
