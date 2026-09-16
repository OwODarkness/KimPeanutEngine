#include "asset/detail/content_catalog_builder.h"

#include <algorithm>
#include <filesystem>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace kpengine::asset::detail
{
    namespace
    {
        std::string FoldAscii(std::string_view value)
        {
            std::string folded;
            folded.reserve(value.size());
            for (const char character : value)
            {
                folded.push_back(character >= 'A' && character <= 'Z'
                                     ? static_cast<char>(character - 'A' + 'a')
                                     : character);
            }
            return folded;
        }

        std::string NormalizeContentPath(std::string value)
        {
            std::replace(value.begin(), value.end(), '\\', '/');
            while (!value.empty() && value.front() == '/')
            {
                value.erase(value.begin());
            }
            return value;
        }

        AssetType BuiltInAssetType(std::string_view type_name)
        {
            const std::string folded = FoldAscii(type_name);
            if (folded == "model") return AssetType::KPAT_Model;
            if (folded == "material") return AssetType::KPAT_Material;
            if (folded == "texture") return AssetType::KPAT_Texture;
            if (folded == "level") return AssetType::KPAT_Level;
            if (folded == "audio") return AssetType::KPAT_Audio;
            if (folded == "shader") return AssetType::KPAT_Shader;
            if (folded == "shaderprogram") return AssetType::KPAT_ShaderProgram;
            if (folded == "mesh") return AssetType::KPAT_Mesh;
            return AssetType::Undefined;
        }

        AssetType ResolveAssetType(const ContentMetadata &record)
        {
            if (IsAssetTypeValueInExtensionRange(record.asset_type))
            {
                return record.asset_type;
            }
            return BuiltInAssetType(record.type_name);
        }

        bool IsShader(const ContentMetadata &record)
        {
            if (FoldAscii(record.type_name) == "shader")
            {
                return true;
            }
            const std::string path = NormalizeContentPath(record.content_path);
            const std::size_t slash = path.find('/');
            return FoldAscii(path.substr(0, slash)) == "shader";
        }

        bool IsSafeContentPath(std::string_view value)
        {
            const std::filesystem::path path{std::string{value}};
            if (path.empty() || path.is_absolute())
            {
                return false;
            }
            for (const auto &component : path)
            {
                if (component == "..")
                {
                    return false;
                }
            }
            return true;
        }

        std::filesystem::path ProductPath(const std::filesystem::path &archive_root,
                                          const ContentProduct &product)
        {
            const std::string_view texture_extension =
                product.type == ArchiveProductType::Texture ? std::string_view{"texture"}
                                                            : std::string_view{};
            return archive_root / ProductRelativePath(product.type, product.hash,
                                                       texture_extension);
        }

        struct AcceptedRecord
        {
            const ContentMetadata *metadata{};
            std::string stable_key;
            std::size_t node_index{};
        };
    }

    AssetCatalogSnapshot BuildContentAssetCatalog(const ContentCatalogBuildInput &input)
    {
        AssetCatalogSnapshot snapshot;
        snapshot.revision = input.revision;
        if (input.registry == nullptr)
        {
            snapshot.status = AssetCatalogSnapshotStatus::Partial;
            snapshot.diagnostics.push_back(
                {AssetCatalogDiagnosticSeverity::Error,
                 AssetCatalogDiagnosticCode::CatalogAssemblyFailed,
                 "content catalog capture has no registry snapshot", {}});
            return snapshot;
        }

        bool partial = false;
        const auto add_diagnostic = [&snapshot, &partial, &input](
                                        AssetCatalogDiagnosticCode code,
                                        std::string message,
                                        std::string related = {})
        {
            partial = true;
            if (snapshot.diagnostics.size() >= input.limits.max_diagnostics)
            {
                return;
            }
            snapshot.diagnostics.push_back(
                {AssetCatalogDiagnosticSeverity::Warning, code, std::move(message),
                 std::move(related)});
        };

        for (const std::string &diagnostic : input.registry->diagnostics)
        {
            add_diagnostic(AssetCatalogDiagnosticCode::InvalidNode,
                           "content metadata: " + diagnostic);
        }

        std::unordered_map<std::string, AcceptedRecord> by_id;
        std::unordered_set<std::string> paths;
        by_id.reserve(input.registry->records.size());
        paths.reserve(input.registry->records.size());

        for (const ContentMetadata &record : input.registry->records)
        {
            if (record.visibility == ContentVisibility::Internal || IsShader(record))
            {
                continue;
            }
            if (!record.id.IsValid() || !IsSafeContentPath(record.content_path))
            {
                add_diagnostic(AssetCatalogDiagnosticCode::InvalidNode,
                               "content metadata has an invalid identity or path");
                continue;
            }

            const std::string content_id = record.id.ToString();
            const std::string content_path = NormalizeContentPath(record.content_path);
            if (!by_id.emplace(content_id, AcceptedRecord{&record, {}, 0}).second)
            {
                add_diagnostic(AssetCatalogDiagnosticCode::DuplicateStableKey,
                               "duplicate ContentID: " + content_id);
                continue;
            }
            if (!paths.insert(content_path).second)
            {
                add_diagnostic(AssetCatalogDiagnosticCode::InvalidNode,
                               "duplicate content path: " + content_path);
                by_id.erase(content_id);
                continue;
            }
            if (record.status != ContentImportStatus::Ready)
            {
                add_diagnostic(AssetCatalogDiagnosticCode::InvalidNode,
                               "content record is not ready: " + content_path);
                by_id.erase(content_id);
                continue;
            }
            if (record.products.empty())
            {
                add_diagnostic(AssetCatalogDiagnosticCode::ArchiveUnavailable,
                               "content record has no published product: " + content_path);
                by_id.erase(content_id);
                continue;
            }

            const AssetType asset_type = ResolveAssetType(record);
            if (!IsAssetTypeValueInExtensionRange(asset_type))
            {
                add_diagnostic(AssetCatalogDiagnosticCode::UnknownTypeDescriptor,
                               "content record has no registered asset type: " + content_path);
                by_id.erase(content_id);
                continue;
            }

            bool products_exist = true;
            for (const ContentProduct &product : record.products)
            {
                const std::filesystem::path product_path = ProductPath(input.archive_root, product);
                std::error_code error;
                if (!std::filesystem::is_regular_file(product_path, error) || error)
                {
                    products_exist = false;
                    break;
                }
            }
            if (!products_exist)
            {
                add_diagnostic(AssetCatalogDiagnosticCode::ArchiveUnavailable,
                               "content record has a missing product: " + content_path);
                by_id.erase(content_id);
                continue;
            }
        }

        if (by_id.size() > input.limits.max_nodes)
        {
            add_diagnostic(AssetCatalogDiagnosticCode::CaptureLimitExceeded,
                           "content catalog exceeds the configured node limit");
        }

        std::vector<std::string> ordered_ids;
        ordered_ids.reserve(by_id.size());
        for (const auto &[id, record] : by_id)
        {
            (void)record;
            ordered_ids.push_back(id);
        }
        std::sort(ordered_ids.begin(), ordered_ids.end());
        if (ordered_ids.size() > input.limits.max_nodes)
        {
            ordered_ids.resize(input.limits.max_nodes);
        }

        std::unordered_map<std::string, std::size_t> node_by_id;
        node_by_id.reserve(ordered_ids.size());
        std::vector<AcceptedRecord> accepted;
        accepted.reserve(ordered_ids.size());
        for (const std::string &id : ordered_ids)
        {
            AcceptedRecord entry = by_id.at(id);
            entry.stable_key = MakeContentCatalogKey(id);
            entry.node_index = snapshot.nodes.size();

            const ContentMetadata &record = *entry.metadata;
            AssetCatalogNode node;
            node.id.value = static_cast<std::uint32_t>(snapshot.nodes.size());
            node.stable_key = entry.stable_key;
            node.type = ResolveAssetType(record);
            node.type_name = record.type_name;
            node.display_name = record.name.empty()
                                    ? std::filesystem::path(record.content_path).filename().string()
                                    : record.name;
            node.content_id = id;
            node.logical_path = NormalizeContentPath(record.content_path);
            node.availability = AssetCatalogAvailability::ArchiveOnly;
            node.dependency_coverage = AssetCatalogDependencyCoverage::Unknown;
            node.schema_version = static_cast<std::uint32_t>(std::max(record.schema_version, 0));

            if (!record.source_path.empty())
            {
                node.provenance.push_back(
                    {NormalizeContentPath(record.source_path), node.display_name, {}, {}});
            }

            const ContentProduct *primary_product = &record.products.front();
            for (const ContentProduct &product : record.products)
            {
                if (static_cast<std::uint16_t>(node.type) ==
                    static_cast<std::uint16_t>(BuiltInAssetType(
                        product.type == ArchiveProductType::Model   ? "model"
                        : product.type == ArchiveProductType::Material ? "material"
                                                                       : "texture")))
                {
                    primary_product = &product;
                    break;
                }
            }
            node.archive_product_type = primary_product->type;
            node.content_hash = primary_product->hash;
            const std::filesystem::path product_path =
                ProductPath(input.archive_root, *primary_product);
            node.product_path = product_path.generic_string();
            std::error_code size_error;
            node.byte_size = std::filesystem::file_size(product_path, size_error);
            if (size_error)
            {
                node.byte_size = 0;
            }

            node_by_id.emplace(id, entry.node_index);
            snapshot.nodes.push_back(std::move(node));
            accepted.push_back(std::move(entry));
        }

        for (const AcceptedRecord &entry : accepted)
        {
            const ContentMetadata &record = *entry.metadata;
            const auto owner = node_by_id.find(record.id.ToString());
            if (owner == node_by_id.end())
            {
                continue;
            }
            for (std::size_t ordinal = 0; ordinal < record.references.size(); ++ordinal)
            {
                if (snapshot.edges.size() >= input.limits.max_edges)
                {
                    add_diagnostic(AssetCatalogDiagnosticCode::CaptureLimitExceeded,
                                   "content catalog exceeds the configured edge limit");
                    break;
                }

                const ContentReference &reference = record.references[ordinal];
                const auto target = node_by_id.find(reference.target.ToString());
                AssetCatalogNodeId target_id{};
                if (target != node_by_id.end())
                {
                    target_id.value = static_cast<std::uint32_t>(target->second);
                }
                else
                {
                    if (snapshot.nodes.size() >= input.limits.max_nodes)
                    {
                        add_diagnostic(AssetCatalogDiagnosticCode::CaptureLimitExceeded,
                                       "content catalog cannot add a missing reference leaf",
                                       MakeContentCatalogKey(record.id.ToString()));
                        continue;
                    }
                    AssetCatalogNode missing;
                    missing.id.value = static_cast<std::uint32_t>(snapshot.nodes.size());
                    missing.kind = AssetCatalogNodeKind::MissingReference;
                    missing.stable_key = MakeMissingReferenceCatalogKey(
                        MakeContentCatalogKey(record.id.ToString()),
                        AssetCatalogRelation::Dependency,
                        static_cast<std::uint32_t>(ordinal), AssetType::Undefined);
                    missing.display_name = "Missing " + reference.target.ToString();
                    missing.type_name = "Content";
                    missing.availability = AssetCatalogAvailability::Missing;
                    missing.dependency_coverage = AssetCatalogDependencyCoverage::Unknown;
                    target_id.value = static_cast<std::uint32_t>(snapshot.nodes.size());
                    const std::string missing_key = missing.stable_key;
                    snapshot.nodes.push_back(std::move(missing));
                    add_diagnostic(AssetCatalogDiagnosticCode::UnresolvedDependency,
                                   "content reference target is not published: " +
                                       reference.target.ToString(),
                                   missing_key);
                }

                snapshot.edges.push_back(
                    {AssetCatalogNodeId{static_cast<std::uint32_t>(owner->second)}, target_id,
                     AssetCatalogRelation::Dependency,
                     static_cast<std::uint32_t>(ordinal), reference.slot});
            }
        }

        snapshot.status = partial ? AssetCatalogSnapshotStatus::Partial
                                  : AssetCatalogSnapshotStatus::Complete;
        std::string diagnostic;
        if (!CanonicalizeAssetCatalogSnapshot(snapshot, diagnostic))
        {
            AssetCatalogSnapshot failure;
            failure.revision = input.revision;
            failure.status = AssetCatalogSnapshotStatus::Partial;
            failure.diagnostics.push_back(
                {AssetCatalogDiagnosticSeverity::Error,
                 AssetCatalogDiagnosticCode::CatalogAssemblyFailed,
                 std::move(diagnostic), {}});
            return failure;
        }
        return snapshot;
    }
}
