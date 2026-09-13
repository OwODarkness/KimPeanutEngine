#include "asset/detail/asset_catalog_builder.h"

#include <algorithm>
#include <limits>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "asset/utility.h"

namespace kpengine::asset::detail
{
    namespace
    {
        constexpr char kUpperHexDigits[] = "0123456789ABCDEF";
        constexpr std::size_t kNoIndex = std::numeric_limits<std::size_t>::max();

        // A descriptor-less type still needs a readable, switch-free name.
        std::string FormatAssetTypeFallback(AssetType type)
        {
            const std::uint16_t value = static_cast<std::uint16_t>(type);
            std::string result = "AssetType 0x";
            for (int shift = 12; shift >= 0; shift -= 4)
            {
                result.push_back(kUpperHexDigits[(value >> shift) & 0xfu]);
            }
            return result;
        }

        AssetType MapArchiveProductType(ArchiveProductType type)
        {
            switch (type)
            {
            case ArchiveProductType::Model:
                return AssetType::KPAT_Model;
            case ArchiveProductType::Material:
                return AssetType::KPAT_Material;
            case ArchiveProductType::Texture:
                return AssetType::KPAT_Texture;
            }
            return AssetType::Undefined;
        }

        std::string ProductIdentityKey(ArchiveProductType type, const ContentHash &content_hash)
        {
            return std::to_string(static_cast<unsigned>(type)) + "/" + content_hash.ToHex();
        }

        // Project-relative when the path lives under the asset root, otherwise
        // the absolute inspection path. Archive storage is internal, so it
        // never becomes a logical path.
        std::string AssetRootRelativeOrAbsolute(const std::string &path,
                                                const std::filesystem::path &asset_root,
                                                bool allow_archive)
        {
            if (path.empty())
            {
                return {};
            }
            std::error_code error;
            const std::filesystem::path candidate =
                std::filesystem::absolute(std::filesystem::path(path), error).lexically_normal();
            if (error)
            {
                return std::filesystem::path(path).generic_string();
            }
            const std::filesystem::path root =
                std::filesystem::absolute(asset_root, error).lexically_normal();
            if (error || root.empty())
            {
                return candidate.generic_string();
            }

            const std::filesystem::path relative = candidate.lexically_relative(root);
            if (relative.empty() || relative == std::filesystem::path("."))
            {
                return candidate.generic_string();
            }
            for (const std::filesystem::path &component : relative)
            {
                if (component == "..")
                {
                    return candidate.generic_string();
                }
            }
            const std::string result = relative.generic_string();
            if (!allow_archive &&
                (result == ".archive" || result.rfind(".archive/", 0) == 0))
            {
                return candidate.generic_string();
            }
            return result;
        }

        // Mirrors the logical Model lookup: drop the source extension, then
        // apply the same canonical form used by AssetManager path keys.
        std::string LogicalPathFromSourcePath(const std::string &source_path)
        {
            try
            {
                const std::filesystem::path stripped =
                    std::filesystem::path(source_path).replace_extension();
                if (stripped.empty())
                {
                    return {};
                }
                return NormalizeAssetRelativePath(stripped.generic_string());
            }
            catch (const ModelArchiveError &)
            {
                return {};
            }
        }

        std::string FileNameOf(const std::string &path)
        {
            const std::string filename = std::filesystem::path(path).filename().generic_string();
            return filename == "." ? std::string{} : filename;
        }

        class CatalogAssembler final
        {
        public:
            explicit CatalogAssembler(const AssetCatalogBuildInput &input)
                : input_(input), limits_(input.limits), live_(input.live)
            {
                // One diagnostic slot is reserved so a truncated set can still
                // explain itself.
                optional_budget_ =
                    limits_.max_diagnostics == 0 ? 0 : limits_.max_diagnostics - 1;
            }

            AssetCatalogSnapshot Build()
            {
                SortLiveRecords();
                if (!input_.live_limit_diagnostic.empty())
                {
                    live_.clear();
                    AddLimitDiagnostic(input_.live_limit_diagnostic);
                }
                BuildTypeNameIndex();

                AppendArchiveNodes();
                if (AppendLiveNodes())
                {
                    EmitLiveEdges();
                }

                AssetCatalogSnapshot snapshot;
                snapshot.revision = input_.revision;
                snapshot.status = partial_ || truncated_
                                      ? AssetCatalogSnapshotStatus::Partial
                                      : AssetCatalogSnapshotStatus::Complete;
                snapshot.nodes = std::move(nodes_);
                snapshot.edges = std::move(edges_);
                AppendDiagnostics(snapshot.diagnostics);

                std::string diagnostic;
                if (!CanonicalizeAssetCatalogSnapshot(snapshot, diagnostic) ||
                    !ValidateAssetCatalogSnapshot(snapshot, diagnostic))
                {
                    return MakeAssemblyFailure(diagnostic);
                }
                return snapshot;
            }

        private:
            struct ProductReference
            {
                const ArchiveCatalogSource *source{};
                const SourceProductRecord *link{};
            };

            // Which archive product, if any, one live record merges into.
            struct LiveTarget
            {
                std::size_t archive_product{kNoIndex};
                bool type_mismatch{};
            };

            void SortLiveRecords()
            {
                // Cache iteration order is unspecified, so every later decision
                // is taken over one deterministic order.
                std::sort(live_.begin(), live_.end(),
                          [](const LiveCatalogRecord &lhs, const LiveCatalogRecord &rhs)
                          { return lhs.id.Pack() < rhs.id.Pack(); });
            }

            void BuildTypeNameIndex()
            {
                std::vector<TypeNameRecord> declared = input_.type_names;
                std::sort(declared.begin(), declared.end());
                for (const TypeNameRecord &entry : declared)
                {
                    if (!entry.name.empty())
                    {
                        type_names_.emplace(entry.type, entry.name);
                    }
                }
                for (const LiveCatalogRecord &record : live_)
                {
                    if (!record.type_name.empty())
                    {
                        type_names_.emplace(record.type, record.type_name);
                    }
                }
            }

            std::string ResolveTypeName(AssetType type) const
            {
                const auto found = type_names_.find(type);
                if (found != type_names_.end())
                {
                    return found->second;
                }
                return FormatAssetTypeFallback(type);
            }

            void AppendNode(AssetCatalogNode node)
            {
                node.id.value = static_cast<std::uint32_t>(nodes_.size());
                nodes_.push_back(std::move(node));
            }

            void AddDiagnostic(AssetCatalogDiagnosticCode code,
                               AssetCatalogDiagnosticSeverity severity, std::string message,
                               std::string related_key, bool required)
            {
                if (severity == AssetCatalogDiagnosticSeverity::Error)
                {
                    // A Complete snapshot may not carry an error diagnostic.
                    partial_ = true;
                }
                AssetCatalogDiagnostic entry;
                entry.severity = severity;
                entry.code = code;
                entry.message = std::move(message);
                entry.related_stable_key = std::move(related_key);
                if (required)
                {
                    // Contract-required entries are never dropped: a
                    // missing-reference node is invalid without its
                    // unresolved-dependency diagnostic.
                    required_diagnostics_.push_back(std::move(entry));
                    return;
                }
                if (optional_diagnostics_.size() < optional_budget_)
                {
                    optional_diagnostics_.push_back(std::move(entry));
                    return;
                }
                truncated_ = true;
            }

            void AppendDiagnostics(std::vector<AssetCatalogDiagnostic> &target)
            {
                target = required_diagnostics_;
                std::size_t remaining = 0;
                if (target.size() < limits_.max_diagnostics)
                {
                    remaining = limits_.max_diagnostics - target.size();
                }
                else
                {
                    truncated_ = true;
                }

                // Keep one slot for the truncation notice when the required
                // entries did not already consume the whole budget.
                const std::size_t take =
                    remaining == 0 ? 0 : std::min(optional_diagnostics_.size(), remaining - 1);
                if (take < optional_diagnostics_.size())
                {
                    truncated_ = true;
                }
                target.insert(target.end(), optional_diagnostics_.begin(),
                              optional_diagnostics_.begin() + take);

                if (!truncated_ || target.size() >= limits_.max_diagnostics)
                {
                    // Either nothing was dropped, or the required entries
                    // already filled the budget and only the notice is lost.
                    return;
                }
                AssetCatalogDiagnostic notice;
                notice.severity = AssetCatalogDiagnosticSeverity::Warning;
                notice.code = AssetCatalogDiagnosticCode::CaptureLimitExceeded;
                notice.message =
                    "asset catalog diagnostics were truncated at the configured limit";
                target.push_back(std::move(notice));
            }

            void AddLimitDiagnostic(std::string message)
            {
                partial_ = true;
                AddDiagnostic(AssetCatalogDiagnosticCode::CaptureLimitExceeded,
                              AssetCatalogDiagnosticSeverity::Error, std::move(message), {},
                              false);
            }

            static AssetCatalogProvenance MakeProvenance(const ArchiveCatalogSource &source)
            {
                AssetCatalogProvenance entry;
                entry.source_path = source.source.normalized_path;
                entry.source_display_name = source.source.display_name;
                for (const SourceDependencyRecord &dependency : source.dependencies)
                {
                    if (!dependency.normalized_path.empty())
                    {
                        entry.source_dependency_paths.push_back(dependency.normalized_path);
                    }
                }
                for (const MaterialOverrideRecord &material_override : source.material_overrides)
                {
                    // An empty authored path is not representable in provenance.
                    if (!material_override.authored_path.empty())
                    {
                        entry.material_overrides.push_back(
                            {material_override.slot, material_override.authored_path});
                    }
                }
                return entry;
            }

            void TruncateAliases(AssetCatalogNode &node)
            {
                std::sort(node.aliases.begin(), node.aliases.end());
                node.aliases.erase(std::unique(node.aliases.begin(), node.aliases.end()),
                                   node.aliases.end());
                node.aliases.erase(
                    std::remove_if(node.aliases.begin(), node.aliases.end(),
                                   [](const std::string &alias) { return alias.empty(); }),
                    node.aliases.end());
                if (node.aliases.size() <= limits_.max_aliases_per_node)
                {
                    return;
                }
                node.aliases.resize(limits_.max_aliases_per_node);
                AddLimitDiagnostic("aliases for " + node.stable_key +
                                   " were truncated at the configured per-node limit");
            }

            void AppendArchiveNodes()
            {
                if (input_.archive == nullptr)
                {
                    partial_ = true;
                    AddDiagnostic(input_.archive_diagnostic_code,
                                  input_.archive_diagnostic_severity,
                                  input_.archive_diagnostic, {}, false);
                    return;
                }

                const ModelArchiveCatalogSnapshot &catalog = *input_.archive;
                if (catalog.products.size() > limits_.max_nodes)
                {
                    AddLimitDiagnostic("archive holds " +
                                       std::to_string(catalog.products.size()) +
                                       " products, over the catalog node limit of " +
                                       std::to_string(limits_.max_nodes));
                    return;
                }

                std::unordered_map<std::string, std::size_t> product_index;
                product_index.reserve(catalog.products.size());
                for (std::size_t index = 0; index < catalog.products.size(); ++index)
                {
                    const ProductRecord &product = catalog.products[index];
                    const std::string identity =
                        ProductIdentityKey(product.asset_type, product.content_hash);
                    product_index.emplace(identity, index);
                    product_path_index_.emplace(
                        CanonicalAssetPathKey(
                            (input_.archive_root / product.relative_path).generic_string()),
                        std::make_pair(product.asset_type, index));
                }

                std::vector<std::vector<ProductReference>> references(catalog.products.size());
                for (const ArchiveCatalogSource &source : catalog.sources)
                {
                    if (source.source.status == SourceImportStatus::Failed)
                    {
                        partial_ = true;
                        AddDiagnostic(
                            AssetCatalogDiagnosticCode::ArchiveSourceFailed,
                            AssetCatalogDiagnosticSeverity::Warning,
                            "import source '" + source.source.normalized_path +
                                "' is recorded as failed: " +
                                (source.source.diagnostic.empty()
                                     ? std::string("no diagnostic recorded")
                                     : source.source.diagnostic),
                            {}, false);
                    }
                    for (const SourceProductRecord &link : source.source_products)
                    {
                        const auto found =
                            product_index.find(ProductIdentityKey(link.asset_type,
                                                                  link.content_hash));
                        if (found == product_index.end())
                        {
                            // ReadCatalog rejects these, so reaching here means
                            // a caller supplied a hand-built catalog.
                            continue;
                        }
                        references[found->second].push_back({&source, &link});
                    }
                }

                archive_node_index_.assign(catalog.products.size(), kNoIndex);
                archive_has_readable_name_.assign(catalog.products.size(), false);
                for (std::size_t index = 0; index < catalog.products.size(); ++index)
                {
                    AppendArchiveNode(catalog.products[index], references[index], index);
                }
            }

            void AppendArchiveNode(const ProductRecord &product,
                                   const std::vector<ProductReference> &references,
                                   std::size_t product_index)
            {
                AssetCatalogNode node;
                node.kind = AssetCatalogNodeKind::Asset;
                node.type = MapArchiveProductType(product.asset_type);
                node.type_name = ResolveTypeName(node.type);
                node.stable_key =
                    MakeArchiveProductCatalogKey(product.asset_type, product.content_hash);
                node.availability = AssetCatalogAvailability::ArchiveOnly;
                node.dependency_coverage = AssetCatalogDependencyCoverage::Unknown;
                node.archive_product_type = product.asset_type;
                node.content_hash = product.content_hash;
                node.byte_size = product.byte_size;
                node.schema_version = product.schema_version;
                node.product_path = AssetRootRelativeOrAbsolute(
                    (input_.archive_root / product.relative_path).generic_string(),
                    input_.asset_root, true);

                const bool root_model = product.asset_type == ArchiveProductType::Model;
                std::string best_alias;
                std::string best_logical_path;
                std::string owning_source_name;
                std::unordered_set<std::string> seen_sources;
                for (const ProductReference &reference : references)
                {
                    if (!reference.link->display_name.empty() &&
                        (best_alias.empty() || reference.link->display_name < best_alias))
                    {
                        best_alias = reference.link->display_name;
                    }
                    if (root_model)
                    {
                        // The lexicographically first logical path wins when
                        // several sources alias one Model product.
                        const std::string candidate =
                            LogicalPathFromSourcePath(reference.source->source.normalized_path);
                        if (!candidate.empty() &&
                            (best_logical_path.empty() || candidate < best_logical_path))
                        {
                            best_logical_path = candidate;
                        }
                    }
                    // Lexicographically first again: several sources can import
                    // the same root Model, and no row order is an identity.
                    const std::string &candidate_name =
                        reference.source->source.display_name;
                    if (!candidate_name.empty() &&
                        (owning_source_name.empty() || candidate_name < owning_source_name))
                    {
                        owning_source_name = candidate_name;
                    }
                    if (seen_sources.insert(reference.source->source.normalized_path).second)
                    {
                        node.provenance.push_back(MakeProvenance(*reference.source));
                        node.aliases.push_back(reference.link->display_name);
                    }
                }

                // Priority: a linked source-product name, then the owning
                // source's own name for its root Model, then an honest
                // type-plus-hash fallback.
                bool readable = false;
                if (!best_alias.empty())
                {
                    node.display_name = best_alias;
                    readable = true;
                }
                else if (root_model && !owning_source_name.empty())
                {
                    node.display_name = owning_source_name;
                    readable = true;
                }
                else
                {
                    node.display_name =
                        node.type_name + " " + product.content_hash.ToHex().substr(0, 8);
                }
                node.logical_path = best_logical_path;

                archive_node_index_[product_index] = nodes_.size();
                archive_has_readable_name_[product_index] = readable;
                TruncateAliases(node);
                AppendNode(std::move(node));
            }

            // A live record joins a cataloged product only when its canonical
            // path and mapped type both agree. Everything else stays a separate
            // runtime identity.
            void ResolveLiveTargets()
            {
                live_targets_.assign(live_.size(), LiveTarget{});
                archive_join_taken_.assign(archive_node_index_.size(), false);
                for (std::size_t index = 0; index < live_.size(); ++index)
                {
                    const LiveCatalogRecord &record = live_[index];
                    if (record.path.empty())
                    {
                        continue;
                    }
                    const auto found =
                        product_path_index_.find(CanonicalAssetPathKey(record.path));
                    if (found == product_path_index_.end())
                    {
                        continue;
                    }
                    if (MapArchiveProductType(found->second.first) != record.type)
                    {
                        live_targets_[index].type_mismatch = true;
                        continue;
                    }
                    // Two live records can share one product path. Only the
                    // first, in sorted order, merges: merging both would emit
                    // duplicate (from, relation, ordinal) edges.
                    if (archive_join_taken_[found->second.second])
                    {
                        continue;
                    }
                    archive_join_taken_[found->second.second] = true;
                    live_targets_[index].archive_product = found->second.second;
                }
            }

            bool AppendLiveNodes()
            {
                if (live_.empty())
                {
                    return false;
                }
                ResolveLiveTargets();

                std::size_t new_nodes = 0;
                for (const LiveTarget &target : live_targets_)
                {
                    if (target.archive_product == kNoIndex)
                    {
                        ++new_nodes;
                    }
                }
                if (nodes_.size() + new_nodes > limits_.max_nodes)
                {
                    // No partial selection of unordered cache entries is
                    // published; the archive portion still is.
                    AddLimitDiagnostic("the live asset graph needs " +
                                       std::to_string(new_nodes) +
                                       " additional nodes, over the catalog node limit of " +
                                       std::to_string(limits_.max_nodes));
                    return false;
                }

                for (std::size_t index = 0; index < live_.size(); ++index)
                {
                    if (live_targets_[index].type_mismatch)
                    {
                        partial_ = true;
                        AddDiagnostic(
                            AssetCatalogDiagnosticCode::InvalidArchiveLiveJoin,
                            AssetCatalogDiagnosticSeverity::Error,
                            "live asset '" + live_[index].path +
                                "' matches an archive product path of a different type",
                            {}, false);
                    }
                    if (live_targets_[index].archive_product != kNoIndex)
                    {
                        MergeLiveIntoArchiveNode(index);
                    }
                    else
                    {
                        AppendRuntimeNode(index);
                    }
                }
                return true;
            }

            void MergeLiveIntoArchiveNode(std::size_t live_index)
            {
                const LiveCatalogRecord &record = live_[live_index];
                const std::size_t product_index = live_targets_[live_index].archive_product;
                AssetCatalogNode &node = nodes_[archive_node_index_[product_index]];
                node.availability = AssetCatalogAvailability::LoadedArchiveProduct;
                node.dependency_coverage = AssetCatalogDependencyCoverage::Complete;
                node.packed_runtime_asset_id = record.id.Pack();
                if (!record.type_name.empty() && node.type_name.empty())
                {
                    node.type_name = record.type_name;
                }

                // The archive alias already came from authored naming, so it
                // outranks the transient runtime name.
                if (archive_has_readable_name_[product_index])
                {
                    if (!record.name.empty())
                    {
                        node.aliases.push_back(record.name);
                    }
                }
                else if (!record.name.empty())
                {
                    node.display_name = record.name;
                }
                else if (!ExtractNameFromPath(record.path).empty())
                {
                    node.display_name = ExtractNameFromPath(record.path);
                }
                else if (!FileNameOf(node.product_path).empty())
                {
                    node.display_name = FileNameOf(node.product_path);
                }
                else
                {
                    node.display_name = node.type_name + " " + std::to_string(record.id.Pack());
                }
                TruncateAliases(node);
                live_node_index_[record.id.Pack()] = archive_node_index_[product_index];
            }

            void AppendRuntimeNode(std::size_t live_index)
            {
                const LiveCatalogRecord &record = live_[live_index];
                AssetCatalogNode node;
                node.kind = AssetCatalogNodeKind::Asset;
                node.type = record.type;
                node.type_name =
                    record.type_name.empty() ? ResolveTypeName(record.type) : record.type_name;
                node.stable_key =
                    record.path_indexed
                        ? MakeRuntimePathCatalogKey(record.type,
                                                    CanonicalAssetPathKey(record.path))
                        : MakeRuntimeIdentityCatalogKey(record.id);
                node.availability = AssetCatalogAvailability::RuntimeOnly;
                node.dependency_coverage = AssetCatalogDependencyCoverage::Complete;
                node.packed_runtime_asset_id = record.id.Pack();
                node.logical_path =
                    AssetRootRelativeOrAbsolute(record.path, input_.asset_root, false);
                if (!record.name.empty())
                {
                    node.display_name = record.name;
                }
                else if (!ExtractNameFromPath(record.path).empty())
                {
                    node.display_name = ExtractNameFromPath(record.path);
                }
                else
                {
                    node.display_name = node.type_name + " " + std::to_string(record.id.Pack());
                }
                if (record.type_name.empty())
                {
                    partial_ = true;
                    AddDiagnostic(AssetCatalogDiagnosticCode::UnknownTypeDescriptor,
                                  AssetCatalogDiagnosticSeverity::Warning,
                                  "no registered descriptor for the live asset type " +
                                      FormatAssetTypeFallback(record.type),
                                  node.stable_key, false);
                }
                live_node_index_[record.id.Pack()] = nodes_.size();
                AppendNode(std::move(node));
            }

            void EmitLiveEdges()
            {
                for (std::size_t index = 0; index < live_.size(); ++index)
                {
                    if (limit_reached_)
                    {
                        return;
                    }
                    EmitEdgesFor(index);
                }
            }

            void EmitEdgesFor(std::size_t live_index)
            {
                const LiveCatalogRecord &record = live_[live_index];
                const auto source_node = live_node_index_.find(record.id.Pack());
                if (source_node == live_node_index_.end())
                {
                    return;
                }
                const std::uint32_t from = static_cast<std::uint32_t>(source_node->second);

                std::unordered_set<std::uint64_t> owned_children;
                owned_children.reserve(record.owned_children.size());
                for (const AssetID &child : record.owned_children)
                {
                    owned_children.insert(child.Pack());
                }

                // Dependency order is preserved, and owned children are
                // re-numbered densely inside their own relation.
                std::uint32_t dependency_ordinal = 0;
                std::uint32_t owned_ordinal = 0;
                for (const AssetID &dependency : record.dependencies)
                {
                    if (owned_children.count(dependency.Pack()) != 0)
                    {
                        AppendLiveEdge(record, from, dependency,
                                       AssetCatalogRelation::OwnedChild, owned_ordinal);
                        ++owned_ordinal;
                    }
                    else
                    {
                        AppendLiveEdge(record, from, dependency,
                                       AssetCatalogRelation::Dependency, dependency_ordinal);
                        ++dependency_ordinal;
                    }
                }

                // A child absent from the dependency vector is a layout the
                // archive contract does not describe, but the relation is
                // preserved so the corruption stays visible.
                for (const AssetID &child : record.owned_children)
                {
                    if (limit_reached_)
                    {
                        return;
                    }
                    if (std::find(record.dependencies.begin(), record.dependencies.end(),
                                  child) != record.dependencies.end())
                    {
                        continue;
                    }
                    partial_ = true;
                    AddDiagnostic(
                        AssetCatalogDiagnosticCode::InvalidOwnedChildLayout,
                        AssetCatalogDiagnosticSeverity::Error,
                        "live asset owns a child that is absent from its dependency vector",
                        nodes_[from].stable_key, false);
                    AppendLiveEdge(record, from, child, AssetCatalogRelation::OwnedChild,
                                   owned_ordinal);
                    ++owned_ordinal;
                }
            }

            void AppendLiveEdge(const LiveCatalogRecord &record, std::uint32_t from,
                                const AssetID &target, AssetCatalogRelation relation,
                                std::uint32_t ordinal)
            {
                if (limit_reached_)
                {
                    return;
                }
                if (edges_.size() >= limits_.max_edges)
                {
                    limit_reached_ = true;
                    AddLimitDiagnostic("the catalog edge limit of " +
                                       std::to_string(limits_.max_edges) + " was reached");
                    return;
                }

                const std::uint32_t to = ResolveEdgeTarget(record, from, target, relation, ordinal);
                if (relation == AssetCatalogRelation::OwnedChild && to == from)
                {
                    // A self own-child is not representable: the contract
                    // rejects it, and discarding every other catalog fact over
                    // one corrupt relation would be worse than reporting it.
                    partial_ = true;
                    AddDiagnostic(AssetCatalogDiagnosticCode::InvalidOwnedChildLayout,
                                  AssetCatalogDiagnosticSeverity::Error,
                                  "live asset lists itself as its own owned child",
                                  nodes_[from].stable_key, false);
                    return;
                }

                AssetCatalogEdge edge;
                edge.from.value = from;
                edge.to.value = to;
                edge.relation = relation;
                edge.ordinal = ordinal;
                edges_.push_back(std::move(edge));
            }

            std::uint32_t ResolveEdgeTarget(const LiveCatalogRecord &record, std::uint32_t from,
                                           const AssetID &target, AssetCatalogRelation relation,
                                           std::uint32_t ordinal)
            {
                const auto found = live_node_index_.find(target.Pack());
                if (found != live_node_index_.end())
                {
                    return static_cast<std::uint32_t>(found->second);
                }

                // The owner, relation, and ordinal keep a missing target
                // distinct from every other unresolved authored edge.
                const std::string key = MakeMissingReferenceCatalogKey(
                    nodes_[from].stable_key, relation, ordinal, target.type);
                const auto existing = missing_node_index_.find(key);
                if (existing != missing_node_index_.end())
                {
                    return static_cast<std::uint32_t>(existing->second);
                }

                AssetCatalogNode node;
                node.kind = AssetCatalogNodeKind::MissingReference;
                node.type = target.type;
                node.type_name = ResolveTypeName(target.type);
                node.stable_key = key;
                node.display_name = "Missing " + node.type_name;
                node.availability = AssetCatalogAvailability::Missing;
                node.dependency_coverage = AssetCatalogDependencyCoverage::Unknown;

                partial_ = true;
                AddDiagnostic(AssetCatalogDiagnosticCode::UnresolvedDependency,
                              AssetCatalogDiagnosticSeverity::Warning,
                              "live asset references an asset that has no node in this snapshot",
                              key, true);
                const std::size_t index = nodes_.size();
                missing_node_index_.emplace(key, index);
                AppendNode(std::move(node));
                return static_cast<std::uint32_t>(index);
            }

            AssetCatalogSnapshot MakeAssemblyFailure(const std::string &diagnostic) const
            {
                AssetCatalogSnapshot snapshot;
                snapshot.revision = input_.revision;
                snapshot.status = AssetCatalogSnapshotStatus::Partial;
                AssetCatalogDiagnostic entry;
                entry.severity = AssetCatalogDiagnosticSeverity::Error;
                entry.code = AssetCatalogDiagnosticCode::CatalogAssemblyFailed;
                entry.message = "asset catalog assembly failed: " + diagnostic;
                snapshot.diagnostics.push_back(std::move(entry));
                return snapshot;
            }

            const AssetCatalogBuildInput &input_;
            AssetCatalogBuildLimits limits_;
            std::size_t optional_budget_{};

            std::vector<LiveCatalogRecord> live_;
            std::vector<AssetCatalogNode> nodes_;
            std::vector<AssetCatalogEdge> edges_;
            std::vector<AssetCatalogDiagnostic> required_diagnostics_;
            std::vector<AssetCatalogDiagnostic> optional_diagnostics_;

            std::unordered_map<AssetType, std::string> type_names_;
            std::vector<std::size_t> archive_node_index_;
            std::vector<bool> archive_has_readable_name_;
            std::unordered_map<std::string, std::pair<ArchiveProductType, std::size_t>>
                product_path_index_;
            std::vector<LiveTarget> live_targets_;
            std::vector<bool> archive_join_taken_;
            std::unordered_map<std::uint64_t, std::size_t> live_node_index_;
            std::unordered_map<std::string, std::size_t> missing_node_index_;

            bool partial_{};
            bool truncated_{};
            bool limit_reached_{};
        };
    }

    AssetCatalogSnapshot BuildAssetCatalog(const AssetCatalogBuildInput &input)
    {
        CatalogAssembler assembler{input};
        return assembler.Build();
    }
}
