# AB1.0 — Asset Catalog Contract and Fixture

- Status: proposed
- Parent design: [Asset Browser Plans](../PLANS.md)
- Roadmap: [Asset Browser TODO](../TODO.md)
- Parent Asset architecture: [Asset Module Plans](../../PLANS.md)

## Objective

Freeze the value-only catalog contract that AB1.1 will populate and AB1.2–AB1.3 will present. AB1.0 adds contract types, validation/key helpers, and a reusable synthetic graph fixture; it does not enumerate the archive, inspect `AssetManager`, or create Editor UI.

The stage is complete when later work can consume a catalog without deciding identity, ordering, edge, failure, or custom-type semantics again.

## Design question

How can Asset describe archive products and live runtime references in one immutable graph while preserving readable identity, safe refresh, custom asset types, and partial failure without exposing SQLite, cache storage, payloads, or locks?

## Scope

AB1.0 owns:

- public catalog value types and the snapshot-source interface;
- stable selection keys and snapshot-local node IDs;
- dependency versus owned-child relation semantics;
- deterministic node/edge ordering;
- explicit missing-reference and partial-snapshot representation;
- reusable Sponza-shaped synthetic fixture;
- contract validation and headless tests.

AB1.0 does not own:

- archive enumeration or `AssetManager` snapshot capture (AB1.1);
- search, filtering, sorting, or persistent UI selection (AB1.2);
- reference traversal, cycle expansion, text export, or ImGui (AB1.3);
- thumbnails, import/reimport, mutation, filesystem watching, or loading.

## Frozen representation

Add `engine/runtime/asset/asset_catalog.h` with dependency-light value types in `kpengine::asset`. Names below are normative unless implementation discovers a compile-level conflict.

```cpp
struct AssetCatalogNodeId
{
    std::uint32_t value = std::numeric_limits<std::uint32_t>::max();
    bool IsValid() const noexcept;
};

enum class AssetCatalogNodeKind : std::uint8_t
{
    Asset,
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
    Dependency,
    OwnedChild,
};

struct AssetCatalogMaterialOverride
{
    std::int32_t slot{};
    std::string authored_path;
};

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
    std::string stable_key;
    AssetCatalogNodeKind kind{AssetCatalogNodeKind::Asset};
    AssetType type{AssetType::Undefined};
    std::string type_name;
    std::string display_name;
    std::string logical_path;
    std::string product_path;
    AssetCatalogAvailability availability{AssetCatalogAvailability::RuntimeOnly};
    AssetCatalogDependencyCoverage dependency_coverage{
        AssetCatalogDependencyCoverage::Unknown};
    std::optional<std::uint64_t> packed_runtime_asset_id;
    std::optional<ArchiveProductType> archive_product_type;
    std::optional<ContentHash> content_hash;
    std::uint64_t byte_size{};
    std::uint32_t schema_version{};
    std::vector<std::string> aliases;
    std::vector<AssetCatalogProvenance> provenance;
};

struct AssetCatalogEdge
{
    AssetCatalogNodeId from;
    AssetCatalogNodeId to;
    AssetCatalogRelation relation{AssetCatalogRelation::Dependency};
    std::uint32_t ordinal{};
    std::string label;
};
```

The snapshot stores one canonical forward edge table rather than duplicated dependency and referencer arrays. AB1.3 may build forward/reverse indexes from that table; both directions therefore consume identical facts.

```cpp
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
    AssetCatalogDiagnosticSeverity severity;
    AssetCatalogDiagnosticCode code;
    std::string message;
    std::string related_stable_key;
};

enum class AssetCatalogSnapshotStatus : std::uint8_t
{
    Complete,
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

class IAssetCatalogSnapshotSource
{
public:
    virtual ~IAssetCatalogSnapshotSource() = default;
    virtual AssetCatalogSnapshot CaptureAssetCatalog() = 0;
};
```

`CaptureAssetCatalog` is synchronous and returns by value. AB1.1 must keep its lock-held copy phase bounded; asynchronous refresh policy is an Editor concern only if measurement later proves it necessary.

## Identity rules

Node ID and stable key solve different problems:

- `AssetCatalogNodeId` is a dense index into `snapshot.nodes`. Valid IDs are exactly `[0, nodes.size())`, and `nodes[index].id.value == index`. Edges use it for compact lookup only.
- `stable_key` preserves selection across explicit refreshes when the underlying identity is still the same. It is opaque to Editor; only Asset constructs or interprets it.

Stable-key namespaces are frozen as follows:

| Node source | Stable-key input | Refresh behavior |
| --- | --- | --- |
| Archive product, loaded or not | product type numeric value + lowercase SHA-256 | Stable across path/name changes while content is unchanged. |
| Path-indexed live asset with no archive product | Asset type numeric value + the same canonical path key used by `AssetManager` | Stable across a refresh and unload/reload of the same logical path. |
| Runtime-only child without unique path identity | packed `AssetID` | Stable only while that runtime identity remains live. |
| Missing reference | owner stable key + relation + ordinal + expected type | Stable while the unresolved authored edge is unchanged. |

V1 spelling is exact: `asset-catalog-v1/product/<type-decimal>/<lowercase-hash>`,
`asset-catalog-v1/path/<type-decimal>/<canonical-path-key>`,
`asset-catalog-v1/runtime/<packed-id-as-16-lowercase-hex>`, and
`asset-catalog-v1/missing/<sha256-owner-key>/<dependency-or-owned-child>/<ordinal-decimal>/<expected-type-decimal>`.
Decimal fields have no leading zeroes. The missing-reference form hashes its
owner key to avoid delimiter ambiguity; it does not hide a user-facing path,
which remains in the node fields.

Contract helpers in `asset_catalog.cpp` construct these keys. Editor must not concatenate them. Human-readable paths remain separate fields and must never become a fallback identity in UI code.

An archive product joined to a live asset uses the archive-product key, not a second live node. Its availability becomes `LoadedArchiveProduct`, and the packed runtime ID is optional inspection metadata. Aliases are sorted, unique, non-empty display strings; they do not create nodes or edges.

## Edge semantics and ordering

- `Dependency` mirrors an ordered `Asset::dependencies` entry that is not an owned child: the source uses the target and prevents unsafe target unload.
- `OwnedChild` refines an ID present in both `Asset::dependencies` and `Asset::owned_children`: emit one OwnedChild edge, never a duplicate Dependency edge.
- Import source files are provenance fields and never `AssetCatalogEdge`s.
- `ordinal` is the original zero-based position within one `(from, relation)` sequence.
- `label` is optional readable context such as `material[3]` or `base_color_texture`; identity and ordering never depend on it.

Canonical snapshot order:

1. Nodes sort by `stable_key` bytewise ascending.
2. Dense node IDs are assigned after that sort.
3. Edges sort by `from`, relation numeric value, `ordinal`, then target stable key.
4. Each `(from, relation, ordinal)` is unique.
5. Diagnostics sort by severity, code, related key, then message.

No locale-aware comparison is allowed in the contract. Presentation sorting belongs to AB1.2.

## Validity and partial failure

Add `ValidateAssetCatalogSnapshot(const AssetCatalogSnapshot&, std::string&)` as a pure helper. It rejects non-dense IDs, empty/duplicate stable keys, invalid availability/kind/coverage combinations, empty `type_name` on ordinary assets, invalid edge endpoints, duplicate ordinals, unsorted collections, self-owned children, and `Complete` snapshots containing error diagnostics. Loaded nodes have Complete dependency coverage; archive-only and missing nodes have Unknown coverage.

A missing dependency is a valid `MissingReference` node with `type` set to the expected type when known, `availability == Missing`, and one `UnresolvedDependency` diagnostic. This preserves the authored edge. It is not represented by an invalid endpoint.

`Partial` means usable catalog facts were published with one or more recoverable diagnostics. `CaptureAssetCatalog` must return a structurally valid snapshot even when archive access fails. Programmer-contract corruption such as duplicate keys is rejected by validation rather than normalized silently.

## Synthetic fixture

Add a header-only `engine/test/support/asset_catalog_fixture.h` factory named `MakeAssetCatalogContractFixture()`. Keeping it value-only lets Asset and later Editor tests use the same graph without SQLite, files, `AssetManager`, or ImGui.

```text
Level: level/sponza_test.level [RuntimeOnly]
  Dependency[0] -> Model: model/sponza [LoadedArchiveProduct]
                       OwnedChild[0] -> Mesh [RuntimeOnly]
                       Dependency[0] -> Material: stone
                       Dependency[1] -> Material: fabric
  Material: stone -> Texture: shared_albedo
  Material: fabric -> Texture: shared_albedo       (shared node)
                   -> Texture: fabric_normal       (ArchiveOnly)
                   -> Missing: fabric_orm          (unresolved)
                   -> Model: model/sponza          (intentional cycle)
```

The fixture uses deterministic fake hashes and packed IDs. The cycle is deliberately synthetic and malformed; it proves the catalog can describe hostile input without making AB1.0 responsible for traversal. The shared Texture proves graph semantics are not reduced to a strict tree.

## Helpers frozen by AB1.0

`asset_catalog.cpp` provides:

```cpp
std::string MakeArchiveProductCatalogKey(ArchiveProductType type,
                                         const ContentHash &hash);
std::string MakeRuntimePathCatalogKey(AssetType type,
                                      std::string_view canonical_path_key);
std::string MakeRuntimeIdentityCatalogKey(AssetID id);
std::string MakeMissingReferenceCatalogKey(std::string_view owner_key,
                                           AssetCatalogRelation relation,
                                           std::uint32_t ordinal,
                                           AssetType expected_type);
bool CanonicalizeAssetCatalogSnapshot(AssetCatalogSnapshot &snapshot,
                                      std::string &diagnostic);
bool ValidateAssetCatalogSnapshot(const AssetCatalogSnapshot &snapshot,
                                  std::string &diagnostic);
```

Canonicalization sorts nodes, remaps endpoints after dense-ID assignment, sorts edges/diagnostics/provenance, and sorts/deduplicates aliases. It rejects duplicate stable keys, duplicate edge ordinals, or invalid endpoints instead of choosing a winner. Validation is non-mutating and returns the first stable contract error. Neither helper logs, accesses global state, opens files, or takes an Asset lock.

Key text is versioned internally with an `asset-catalog-v1/` prefix. Tests pin exact golden keys so later persistence or selection code cannot accidentally depend on an unstable spelling change.

## Concrete file changes

| File | AB1.0 change |
| --- | --- |
| `engine/runtime/asset/asset_catalog.h` | Add public values, source interface, key declarations, canonicalization, and validation. |
| `engine/runtime/asset/asset_catalog.cpp` | Implement pure keys, canonicalization, and validation. |
| `engine/runtime/asset/CMakeLists.txt` | Compile `asset_catalog.cpp` into `AssetRuntime`; add no Editor dependency. |
| `engine/test/support/asset_catalog_fixture.h` | Add the header-only reusable synthetic graph factory. |
| `engine/test/unit/asset/asset_catalog_contract_test.cpp` | Add key, validity, ordering, failure, custom-type, and fixture tests. |
| `engine/test/unit/asset/CMakeLists.txt` | Add the contract test to `AssetUnitTest`. |

AB1.0 does not edit `AssetManager`, `ModelArchiveDatabase`, Runtime composition, Editor, archive schema, or checked-in asset content.

## Implementation sequence

1. Add the header and compile-only value contract with equality operators needed by tests.
2. Implement the four key builders and pin their exact V1 output.
3. Implement structural preflight plus the stricter public validation so
   failure rules are explicit.
4. Implement canonicalization with preflight, old-to-new node-ID remapping,
   and final public validation.
5. Add the shared fixture and assert that its Partial snapshot is structurally valid despite its cycle and missing target.
6. Add negative tests one invariant at a time.
7. Review the public header for forbidden pointers, callbacks beyond the source interface, Editor/Render/Graphics includes, and hidden filesystem/database ownership.

## Focused test matrix

- Default node ID is invalid; zero is valid after assignment.
- Archive, runtime-path, runtime-identity, and missing-reference golden keys are distinct and deterministic.
- Custom type `0x1000` retains its descriptor-provided `type_name` without a built-in switch.
- Canonicalization produces identical field ordering from multiple insertion orders.
- Edge endpoint remapping remains correct after node sorting.
- Shared targets remain one node with multiple incoming edges.
- Cycles are accepted as graph data; self-owned-child edges are rejected.
- Missing references require `Missing` availability and an unresolved diagnostic.
- Loaded nodes have Complete dependency coverage; archive-only and missing nodes have Unknown coverage.
- Multiple provenance entries remain ordered copied metadata and create no graph edges.
- Duplicate node keys and duplicate `(from, relation, ordinal)` edges fail.
- Complete snapshots with error diagnostics fail; Partial snapshots with usable data pass.
- Empty ordinary type names, invalid IDs/endpoints, and unsorted snapshots fail validation.
- The Sponza-shaped fixture validates and exposes the exact expected node/edge counts and ordered chains.
- Public values are copy/move constructible; the fixture copy can be mutated without changing the original.

Tests deliberately do not instantiate `AssetManager`, SQLite, ImGui, or a graphics backend.

## Acceptance criteria

- [ ] The public contract and helper implementation compile in `AssetRuntime`.
- [ ] Stable-key namespaces and golden values are documented and tested.
- [ ] Dense IDs, canonical order, remapping, relation semantics, and partial-failure rules are tested.
- [ ] Dependency coverage and multi-source provenance are explicit and tested.
- [ ] The reusable fixture contains Level, archive-backed Model/Materials/Textures, runtime-only Mesh, shared Texture, archive-only Texture, missing reference, and cycle.
- [ ] Custom type names come from captured descriptor metadata rather than a central enum switch.
- [ ] The contract exposes no payload, cache, database, filesystem, Editor, Render, or Graphics object.
- [ ] No archive enumeration, manager snapshot, or UI implementation leaks into AB1.0.
- [ ] Focused Asset tests and `git diff --check` pass.

## Validation commands

```powershell
.\tools\kp.ps1 build AssetUnitTest
.\tools\kp.ps1 test Asset
git diff --check
```

A full engine build is optional for AB1.0 because it adds a public AssetRuntime header and pure implementation only. If transitive compile fallout appears, run the standard Debug build and report the first source failure separately from environment failures.

## Handoff to AB1.1

AB1.1 implements `IAssetCatalogSnapshotSource` using read-only archive enumeration plus a bounded live-cache copy. It must construct raw nodes/edges, call the frozen canonicalizer, validate before publication, and never revise identity or relation semantics inside the provider.

AB1.1 may add diagnostics but may not renumber existing codes or change golden key spelling without an explicit catalog-contract version increment and migration note.
