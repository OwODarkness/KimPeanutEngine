#ifndef KPENGINE_RUNTIME_ASSET_ASSET_CATALOG_H
#define KPENGINE_RUNTIME_ASSET_ASSET_CATALOG_H

#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "asset_product.h"
#include "common.h"

namespace kpengine::asset
{
    // Every stable key carries this revision prefix. Consumers treat keys as
    // opaque selection tokens; only Asset constructs or interprets them. A
    // spelling change requires a new prefix and a migration note, never a
    // silent renumbering.
    inline constexpr std::string_view kAssetCatalogKeyPrefix = "asset-catalog-v1/";

    // A dense index into AssetCatalogSnapshot::nodes. Valid ids satisfy
    // nodes[id.value].id == id. It is a lookup accelerator with no meaning
    // outside its owning snapshot.
    struct AssetCatalogNodeId
    {
        std::uint32_t value = std::numeric_limits<std::uint32_t>::max();

        bool IsValid() const noexcept
        {
            return value != std::numeric_limits<std::uint32_t>::max();
        }

        friend bool operator==(const AssetCatalogNodeId &lhs,
                               const AssetCatalogNodeId &rhs) noexcept
        {
            return lhs.value == rhs.value;
        }

        friend bool operator!=(const AssetCatalogNodeId &lhs,
                               const AssetCatalogNodeId &rhs) noexcept
        {
            return !(lhs == rhs);
        }
    };

    enum class AssetCatalogNodeKind : std::uint8_t
    {
        Asset,
        // A named target that an authored edge points at but that no product or
        // live Asset exists for. It preserves the authored edge instead of
        // becoming a dangling endpoint.
        MissingReference,
    };

    enum class AssetCatalogAvailability : std::uint8_t
    {
        ArchiveOnly,
        LoadedArchiveProduct,
        RuntimeOnly,
        Missing,
    };

    enum class AssetCatalogDependencyCoverage : std::uint8_t
    {
        Unknown,
        Complete,
    };

    enum class AssetCatalogRelation : std::uint8_t
    {
        // Mirrors an ordered Asset::dependencies entry that is not an owned
        // child: the source uses the target and protects it from unload.
        Dependency,
        // Refines an id present in both Asset::dependencies and
        // Asset::owned_children. Exactly one OwnedChild edge is emitted, never
        // a duplicate Dependency edge.
        OwnedChild,
    };

    struct AssetCatalogMaterialOverride
    {
        std::int32_t slot{};
        std::string authored_path;
    };

    // Import provenance is copied metadata. Source files are never catalog
    // edges because they are not runtime references.
    struct AssetCatalogProvenance
    {
        std::string source_path;
        std::string source_display_name;
        std::vector<std::string> source_dependency_paths;
        std::vector<AssetCatalogMaterialOverride> material_overrides;
    };

    struct AssetCatalogNode
    {
        AssetCatalogNodeId id;
        // Opaque, refresh-stable selection identity. Construction and
        // interpretation belong to Asset; Editor never concatenates one.
        std::string stable_key;
        AssetCatalogNodeKind kind{AssetCatalogNodeKind::Asset};
        AssetType type{AssetType::Undefined};
        // Registered descriptor name, so custom types need no central switch.
        std::string type_name;
        std::string display_name;
        std::string logical_path;
        std::string product_path;
        AssetCatalogAvailability availability{AssetCatalogAvailability::RuntimeOnly};
        AssetCatalogDependencyCoverage dependency_coverage{
            AssetCatalogDependencyCoverage::Unknown};
        // Inspection metadata for a product joined to a live Asset. It is not
        // an identity and is never a stable-key input for archive products.
        std::optional<std::uint64_t> packed_runtime_asset_id;
        std::optional<ArchiveProductType> archive_product_type;
        std::optional<ContentHash> content_hash;
        std::uint64_t byte_size{};
        std::uint32_t schema_version{};
        std::vector<std::string> aliases;
        std::vector<AssetCatalogProvenance> provenance;
    };

    // The snapshot keeps one canonical forward edge table. Reverse
    // (referencer) projections derive from these identical facts, so both
    // directions can never disagree.
    struct AssetCatalogEdge
    {
        AssetCatalogNodeId from;
        AssetCatalogNodeId to;
        AssetCatalogRelation relation{AssetCatalogRelation::Dependency};
        // Original zero-based position within one (from, relation) sequence.
        std::uint32_t ordinal{};
        // Readable context such as "material[3]". Identity and ordering never
        // depend on it.
        std::string label;

        friend bool operator==(const AssetCatalogEdge &lhs,
                               const AssetCatalogEdge &rhs) noexcept
        {
            return lhs.from == rhs.from && lhs.to == rhs.to &&
                   lhs.relation == rhs.relation && lhs.ordinal == rhs.ordinal &&
                   lhs.label == rhs.label;
        }

        friend bool operator!=(const AssetCatalogEdge &lhs,
                               const AssetCatalogEdge &rhs) noexcept
        {
            return !(lhs == rhs);
        }
    };

    enum class AssetCatalogDiagnosticSeverity : std::uint8_t
    {
        Warning,
        Error,
    };

    enum class AssetCatalogDiagnosticCode : std::uint8_t
    {
        ArchiveUnavailable,
        ArchiveSourceFailed,
        CaptureLimitExceeded,
        UnknownTypeDescriptor,
        UnresolvedDependency,
        InvalidArchiveLiveJoin,
        InvalidOwnedChildLayout,
        CatalogAssemblyFailed,
        DuplicateStableKey,
        InvalidNode,
        InvalidEdge,
    };

    struct AssetCatalogDiagnostic
    {
        AssetCatalogDiagnosticSeverity severity{
            AssetCatalogDiagnosticSeverity::Warning};
        AssetCatalogDiagnosticCode code{AssetCatalogDiagnosticCode::InvalidNode};
        std::string message;
        // Optional stable key of the node the diagnostic is about.
        std::string related_stable_key;
    };

    enum class AssetCatalogSnapshotStatus : std::uint8_t
    {
        Complete,
        // Usable catalog facts were published together with one or more
        // recoverable diagnostics, for example an unavailable archive.
        Partial,
    };

    struct AssetCatalogSnapshot
    {
        std::uint64_t revision{};
        AssetCatalogSnapshotStatus status{AssetCatalogSnapshotStatus::Complete};
        std::vector<AssetCatalogNode> nodes;
        std::vector<AssetCatalogEdge> edges;
        std::vector<AssetCatalogDiagnostic> diagnostics;
    };

    // The provider boundary. Implementations own archive enumeration and the
    // bounded live-cache copy; callers receive a value-only snapshot with no
    // payload, cache, database, or lock reference.
    class IAssetCatalogSnapshotSource
    {
    public:
        virtual ~IAssetCatalogSnapshotSource() = default;
        virtual AssetCatalogSnapshot CaptureAssetCatalog() = 0;
    };

    // Stable-key builders. The V1 spellings are frozen and pinned by golden
    // tests; decimal fields never carry leading zeroes.
    //
    //   asset-catalog-v1/product/<type-decimal>/<lowercase-hash>
    //   asset-catalog-v1/path/<type-decimal>/<canonical-path-key>
    //   asset-catalog-v1/runtime/<packed-id-as-16-lowercase-hex>
    //   asset-catalog-v1/missing/<sha256-owner-key>/<relation>/<ordinal>/<type>
    std::string MakeArchiveProductCatalogKey(ArchiveProductType type,
                                             const ContentHash &hash);
    std::string MakeRuntimePathCatalogKey(AssetType type,
                                          std::string_view canonical_path_key);
    std::string MakeRuntimeIdentityCatalogKey(AssetID id);
    // Hashes the owner key so arbitrary owner text cannot forge a delimiter.
    // The user-facing path stays in the node fields and is never hidden.
    std::string MakeMissingReferenceCatalogKey(std::string_view owner_key,
                                              AssetCatalogRelation relation,
                                              std::uint32_t ordinal,
                                              AssetType expected_type);

    // Sorts nodes by stable key, assigns dense node ids, remaps edge endpoints,
    // and sorts edges, diagnostics, provenance, and aliases. The input must
    // already carry dense node ids, because edge endpoints are interpreted as
    // indices into `nodes`. Rejects duplicate stable keys, duplicate
    // (from, relation, ordinal) edges, and invalid endpoints instead of
    // choosing a winner. On failure `snapshot` is left untouched.
    bool CanonicalizeAssetCatalogSnapshot(AssetCatalogSnapshot &snapshot,
                                          std::string &diagnostic);

    // Non-mutating structural and semantic check returning the first contract
    // error. It never logs, reads files, or takes an Asset lock.
    bool ValidateAssetCatalogSnapshot(const AssetCatalogSnapshot &snapshot,
                                      std::string &diagnostic);
}

#endif
