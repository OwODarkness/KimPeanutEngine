# AB1.1 — Asset Catalog Snapshot Provider

- Status: proposed
- Prerequisite: [AB1.0 — Catalog contract and fixture](AB1.0.md)
- Parent design: [Asset Browser Plans](../PLANS.md)
- Roadmap: [Asset Browser TODO](../TODO.md)
- Parent Asset architecture: [Asset Module Plans](../../PLANS.md)

## Objective

Implement the Asset-owned `IAssetCatalogSnapshotSource` by joining one strict read-only archive inventory with one bounded copy of current `AssetManager` state. Publish a canonical, structurally valid snapshot without loading products, reading payload contents, exposing SQLite/cache objects, or holding an Asset lock after capture.

AB1.1 is complete when AB1.2 can display archive and live nodes, and AB1.3 can derive both graph directions, using only the frozen snapshot contract.

## Design question

How can the provider combine persistent product identity with transient runtime identity while keeping the render-thread refresh bounded, preserving current lock order, describing incomplete archive-only knowledge honestly, and remaining deterministic under unordered caches and concurrent unload?

## Scope

AB1.1 owns:

- one consistent archive catalog query covering every `products` row;
- a short `state_mutex_` copy of live wrappers, type names, dependencies, and owned children;
- archive/live joining and readable alias selection;
- canonical node/edge construction and missing-target synthesis;
- capture revision, partial status, diagnostics, and hard size bounds;
- archive, provider, joining, and concurrency tests.

AB1.1 does not own:

- Editor/Runtime injection or automatic refresh scheduling (AB1.2);
- ImGui, filtering, selection, traversal, reverse indexes, or text export;
- parsing Model/Material/Texture payload bytes to discover archive-only edges;
- archive schema migration, product publication, import, load, or mutation.

## AB1.0 contract dependency

AB1.0 freezes `AssetCatalogDependencyCoverage { Unknown, Complete }` on each
node for this provider:

- A loaded live node is `Complete` because its committed dependency vector is available.
- An archive-only product is `Unknown`; the current archive records product identity and source-product roles but not the full product dependency graph.
- A missing-reference node is `Unknown`.

The UI must later show “dependency data unavailable until loaded” for Unknown coverage; it must not render an archive-only node with zero captured edges as a proven leaf.

AB1.0 also freezes owned-child classification around the current registration
layout: owned children occupy leading dependency slots. The provider emits one
`OwnedChild` edge for a dependency whose ID is in `owned_children`, not both an
OwnedChild and duplicate Dependency edge. Remaining dependencies are
re-numbered densely within the Dependency relation while preserving order.

## Archive catalog read model

`SourceArchiveSnapshot` is optimized for probing one imported source and does not enumerate unlinked global products. In particular, Texture rows can exist in `products` without a `source_products` row. AB1.1 adds a separate catalog read model in `model_archive.h`:

```cpp
struct ArchiveCatalogSource
{
    SourceRecord source;
    std::vector<SourceDependencyRecord> dependencies;
    std::vector<SourceProductRecord> source_products;
    std::vector<MaterialOverrideRecord> material_overrides;
};

struct ModelArchiveCatalogSnapshot
{
    std::vector<ArchiveCatalogSource> sources;
    std::vector<ProductRecord> products;
};

struct ModelArchiveCatalogReadLimits
{
    std::size_t max_sources{100'000};
    std::size_t max_products{250'000};
    std::size_t max_related_records{1'000'000};
};

ModelArchiveCatalogSnapshot ReadCatalog(
    const ModelArchiveCatalogReadLimits &limits = {});
```

`ReadCatalog()` is non-mutating in either open mode. The production catalog
provider always constructs a dedicated `ModelArchiveOpenMode::ReadOnly`
connection; importer code may use the same query for diagnostics without a
second implementation.

It starts one deferred SQLite read transaction, performs ordered set queries, commits, and returns copied values. It does not call `FindSource` in a loop.

Ordered queries cover:

1. `sources ORDER BY normalized_path`;
2. all `products ORDER BY asset_type, content_hash`;
3. `source_dependencies` joined to `sources`, ordered by source path and dependency path;
4. optional dependency metadata with the same order;
5. `source_products` joined to `sources`, ordered by source path, role, and slot;
6. material overrides ordered by source path and slot.

Count queries for sources, products, and the combined dependency/product-link/
override rows run inside the same transaction before result allocation. Values
over the configured limits fail with `ModelArchiveErrorCode::InvalidArgument`;
integer overflow or negative database counts fail as InvalidDatabase.

Rows join to the in-memory source record by database source ID. Missing source IDs, invalid enum values, negative sizes, invalid hashes, duplicate product identities, or broken source-product links fail the archive read as `InvalidDatabase`; they are not silently skipped.

The read does not verify product bytes or call `IntegrityCheck`. Browser refresh reports archive metadata, not a full integrity audit.

## Concrete provider

An Asset-owned provider implements `IAssetCatalogSnapshotSource`; `AssetManager`
grants that provider narrow friend access for the bounded copy phase:

```cpp
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
    std::filesystem::path database_path;
    std::int32_t busy_timeout_ms{50};
    AssetCatalogCaptureLimits limits;
};

class AssetCatalogSnapshotProvider final : public IAssetCatalogSnapshotSource
{
public:
    AssetCatalogSnapshotProvider(AssetManager &assets,
                                 AssetCatalogProviderConfig config);
    AssetCatalogSnapshot CaptureAssetCatalog() override;
};
```

The provider is part of `AssetRuntime`, borrows the process-lifetime manager,
and owns its archive configuration and revision counter. Runtime composition
will own the provider in AB1.2 and expose only `IAssetCatalogSnapshotSource*`
to Editor. A generic public cache-enumeration method is rejected because it
would expose manager storage policy to every caller.

Assembly is separated into an Asset-private pure builder taking copied
`ModelArchiveCatalogSnapshot` and `LiveCatalogRecord` values. Focused tests can
exercise joins and failures without mutating the singleton or opening SQLite;
the production provider remains the only code allowed to capture manager state.

The archive location is the existing `<GetAssetDirectory()>/.archive/archive.sqlite3`. Each explicit capture opens a fresh read-only connection with a 50 ms busy timeout, reads one transaction, and closes it. This supports recovery when the archive is replaced between refreshes and avoids a persistent SQLite object in `AssetManager`. The current archive is small; AB1.2 must not refresh every frame. If measured capture time later exceeds the UI budget, AB1.2 may schedule explicit refresh away from the render frame without changing this synchronous Asset contract.

## Capture phases

### 1. Read archive without Asset locks

Open the archive read-only and call `ReadCatalog()`. Convert open/busy/newer-schema/invalid-database failures into one `ArchiveUnavailable` diagnostic and continue with live capture. A failed import source produces an `ArchiveSourceFailed` warning carrying its normalized path and recorded diagnostic; its valid global products remain eligible for display.

Before materializing rows, enforce:

- at most 100,000 archive sources;
- at most 250,000 products/nodes overall;
- at most 1,000,000 combined provenance/link records;
- at most 1,000,000 edges overall;
- at most 64 aliases per node;
- at most 1,024 diagnostics.

Archive count overflow abandons the archive portion and publishes the live graph with `CaptureLimitExceeded`. Live-count overflow publishes no partial selection of unordered cache entries; it returns a valid Partial snapshot containing the limit diagnostic. Diagnostic overflow is represented by one final truncation diagnostic.

### 2. Copy live Asset state under `state_mutex_`

Copy only internal plain records:

```cpp
struct LiveCatalogRecord
{
    AssetID id;
    AssetType type;
    std::string type_name;
    std::string name;
    std::string path;
    bool path_indexed{};
    std::vector<AssetID> dependencies;
    std::vector<AssetID> owned_children;
};
```

For every non-null, generation-valid cache slot:

- copy the wrapper ID, name, path, dependencies, and owned children directly;
- resolve `type_name` from `AssetTypeRegistry::FindByType` while the registry is protected;
- mark `path_indexed` only when `cache.path_index[Key(path)]` equals this exact ID;
- copy no payload, `ref_assets`, loader callback, descriptor pointer, cache pointer, or handle-system object.

The copy phase never takes `load_mutex_`, opens SQLite, logs, canonicalizes strings, invokes external callbacks, or allocates based on archive counts. It reserves from the already-counted live slots and releases `state_mutex_` before all joining and diagnostics. Existing lock order is therefore unchanged.

Concurrent registration/unload is linearized at this copy phase. The returned snapshot may become stale immediately after unlock; that is expected for a read-only UI snapshot. All copied endpoints are resolved against the same live record set, so one snapshot never mixes dependency vectors from two lock epochs.

### 3. Build archive nodes

Create one node per unique global Product `(ArchiveProductType, ContentHash)`:

- map Model/Material/Texture to the corresponding built-in `AssetType`;
- use the AB1.0 archive-product stable key;
- set product path, hash, size, schema, `ArchiveOnly`, and Unknown dependency coverage;
- collect every matching `source_products` display name as a sorted unique alias;
- attach source/logical paths when one source owns the alias.

For a root Model source-product link, derive `logical_path` by removing the
source extension with the same normalization used by logical Model lookup. If
multiple sources alias one Model product, choose the lexicographically first
logical path and keep every source in provenance/aliases. Generated Materials
and unlinked Textures have no invented logical path. Their content-addressed
`.archive/...` path remains `product_path`.

Primary display name priority is:

1. lexicographically first non-empty source-product display name;
2. source display name for its root Model product;
3. `<type-name> <first-8-hash-characters>`.

The fallback is intentionally readable but honest; AB1.1 does not parse Material products to invent Texture semantics. Multiple source aliases remain on one product node. Products without source-product links, including current Texture records, still appear.

Failed source records add diagnostics but do not create fake asset nodes. Source dependencies and material overrides are retained as provenance associated with matching product nodes, never converted into runtime reference edges.

### 4. Join live nodes

Build a canonical absolute-path index from archive `ProductRecord::relative_path`. For each live record:

1. If its canonical path matches a cataloged product of the same mapped type, merge into that product node, set `LoadedArchiveProduct`, Complete dependency coverage, packed runtime ID, and add the live name as an alias.
2. Otherwise, if it is path-indexed, create a RuntimeOnly node with the AB1.0 runtime-path key.
3. Otherwise create a RuntimeOnly node with the packed runtime-identity key.

Type mismatch at a matching product path is not merged; it produces an `InvalidArchiveLiveJoin` error and keeps the live node separate. A missing type descriptor uses `AssetType 0xNNNN` as its visible type name and emits `UnknownTypeDescriptor`. It never adds a central custom-type switch.

Live primary display name priority is archive alias, non-empty `Asset::name`, logical filename, product filename, then `<type-name> <packed-id>`. A non-archive path under `GetAssetDirectory()` becomes the live node's project-relative `logical_path`; paths outside it remain generic absolute inspection paths. Stable keys still use the exact AB1.0 key helpers.

### 5. Emit live edges

Build a packed-`AssetID` to node-key map from the copied live records. For each source record:

- preserve the dependency vector's original order;
- classify an ID present in `owned_children` as one `OwnedChild` edge and do not emit a duplicate Dependency edge;
- assign dense zero-based ordinals independently to Dependency and OwnedChild edges;
- leave `label` empty in AB1.1 because the wrapper does not preserve authored parameter/slot labels;
- emit an OwnedChild edge for a copied child absent from `dependencies`, add `InvalidOwnedChildLayout`, and mark the snapshot Partial;
- never trust or use the mutable `ref_assets` reverse list; AB1.3 derives referencers from the canonical forward table.

If a dependency ID has no node in the copied live set, synthesize one MissingReference node using the source key, relation, ordinal, and expected type from the missing ID. Its display name is `Missing <type-name>`, its path is empty, and it receives `UnresolvedDependency`. This case should not occur for a valid committed graph, but preserving the edge makes corruption visible and keeps the snapshot structurally valid.

Repeated references to the same valid target remain distinct edges when their ordinals differ. Shared targets remain one node. Cycles are retained without traversal.

### 6. Canonicalize and publish

Call the AB1.0 canonicalizer once after all raw nodes, edges, aliases, provenance, and diagnostics are assembled. Then call public validation and publish only the validated value.

`revision` is a non-zero process-local capture sequence allocated from the
provider's atomic counter before either read phase; every capture attempt gets
a new value even if content is unchanged. It is not an archive generation and
does not promise semantic change detection. Editor preserves selection by
stable key, never revision.

Set status to Partial when archive access/limits fail, a source is failed, a join/type/owned-child issue occurs, a missing node is synthesized, or diagnostics are truncated. Warnings alone may therefore accompany Partial. Complete means both sources were captured without provider diagnostics; it does not mean archive-only dependency coverage is complete.

If final canonicalization or validation fails because of a provider programming error, return a minimal valid Partial snapshot with the assigned revision and one `CatalogAssemblyFailed` error. Do not publish a malformed graph or throw into Editor.

## Provenance contract amendment

AB1.0's singular `source_path` cannot represent a content product shared by multiple source aliases. Add:

```cpp
struct AssetCatalogProvenance
{
    std::string source_path;
    std::string source_display_name;
    std::vector<std::string> source_dependency_paths;
    std::vector<AssetCatalogMaterialOverride> material_overrides;
};
```

`AssetCatalogNode` stores `std::vector<AssetCatalogProvenance> provenance` and drops singular `source_path`. Provenance sorts by source path; dependency paths and overrides keep their archive-defined order. This is copied authoring metadata only. It creates no graph edges and AB1.1 never resolves or reads the referenced source files.

## Threading and lifetime

```text
CaptureAssetCatalog
  allocate provider-local atomic revision
  -> read-only SQLite transaction (no Asset lock)
  -> state_mutex_ live copy (no SQLite/load lock)
  -> unlock
  -> join/canonicalize/validate local values
  -> return by value
```

No external code executes while `state_mutex_` is held. No lock is nested with the SQLite transaction. No returned object refers to a wrapper, registry descriptor, callback, payload, cache, statement, or database connection. Temporary records and connections die before return.

## Concrete file changes

| File | AB1.1 change |
| --- | --- |
| `engine/runtime/asset/model_archive.h/.cpp` | Add `ModelArchiveCatalogSnapshot`, `ArchiveCatalogSource`, and transactional `ReadCatalog()`. |
| `engine/runtime/asset/asset_catalog.h/.cpp` | Apply the AB1.0 coverage/provenance/diagnostic clarifications and keep canonical validation authoritative. |
| `engine/runtime/asset/asset_catalog_snapshot_provider.h/.cpp` | Add provider config, limits, capture orchestration, live-copy phase, joining, and publication fallback. |
| `engine/runtime/asset/detail/asset_catalog_builder.h/.cpp` | Add Asset-private copied live records and pure archive/live graph assembly. |
| `engine/runtime/asset/asset_manager.h` | Declare the provider friend only; add no public enumeration API. |
| `engine/runtime/asset/CMakeLists.txt` | Compile provider/builder into `AssetRuntime`; `AssetArchive` remains the database owner. |
| `engine/test/unit/asset/model_archive_database_test.cpp` | Add catalog enumeration, ordering, global Texture, failed-source, read-only, and corruption tests. |
| `engine/test/unit/asset/asset_catalog_builder_test.cpp` | Test pure joins, aliases, relations, missing nodes, bounds, and deterministic output. |
| `engine/test/unit/asset/asset_catalog_provider_test.cpp` | Test a clean-process provider, runtime-only custom asset, archive failure, revision, and lock-safe concurrency. |
| `engine/test/unit/asset/CMakeLists.txt` | Add builder coverage and a separate `AssetCatalogProviderTest` executable. |

`AssetCatalogProviderTest` is separate from `AssetUnitTest` so the singleton type registry and cache start clean. It copies the existing Assimp runtime DLL for Windows for the same reason as other `AssetRuntime` test targets.

AB1.1 does not edit RuntimeContext, Engine, Editor, archive schema, import code, product codecs, or checked-in assets.

## Implementation sequence

1. Amend AB1.0 coverage, provenance, diagnostic codes, validation, and fixture expectations; rerun its contract tests.
2. Add `ReadCatalog()` and its database tests, using one deferred read transaction and set-oriented ordered queries.
3. Add the private builder inputs and assemble archive-only nodes first.
4. Implement deterministic live joins and display-name/alias fallbacks.
5. Emit classified Dependency/OwnedChild edges and synthesize missing nodes.
6. Enforce limits, canonicalize, validate, and implement minimal-valid fallback.
7. Add the provider with configurable database path/timeout and the short friend-access live copy.
8. Add clean-process integration/concurrency tests and review lock boundaries.

## Focused test matrix

### Archive repository

- Empty initialized read-only archive returns empty ordered values.
- Multiple sources/products return deterministic ordering independent of insertion order.
- A global Texture row without `source_products` is included.
- Shared Model/Material product links remain one global ProductRecord with multiple source links.
- Failed sources preserve their status/diagnostic.
- Optional dependency metadata absence preserves dependency records with zero metadata.
- Read-only and read-write `ReadCatalog()` calls return the same values; corrupt
  enums/hashes/sizes, broken links, and configured count overflow fail
  deterministically.
- A concurrent writer commits either before or after the read transaction; no mixed catalog epoch is observed.

### Pure builder

- Archive-only products have Unknown coverage and readable type-plus-short-hash fallback.
- Product path/type match merges live/archive identity; mismatch stays separate with a diagnostic.
- Archive aliases are sorted/deduplicated and choose the documented primary display name.
- Path-indexed and runtime-only stable keys follow AB1.0.
- Owned children become one OwnedChild edge despite also occupying dependency slots.
- Dependency ordinals remain dense after owned-child classification.
- Shared references, repeated ordered references, and cycles are preserved.
- Missing live endpoints synthesize deterministic MissingReference nodes.
- Unknown custom descriptors use hexadecimal fallback names and diagnostics.
- Archive/live/edge/alias/diagnostic bounds produce deterministic Partial results.
- Different raw insertion orders canonicalize to field-identical snapshots except revision.

### Provider and concurrency

- Missing/busy/invalid archive returns a valid Partial live snapshot within the configured timeout envelope.
- A runtime-only test custom asset is visible without payload data crossing the contract.
- Consecutive captures have increasing non-zero revisions and stable node keys.
- Concurrent `GetAsset`, load completion, and allowed unload either precede or follow the one locked live-copy epoch; snapshots always validate.
- Capture never waits on `load_mutex_`, invokes a loader, mutates `ref_assets`, or changes live counts.
- Destroying the provider releases its database/configuration state and does not affect `AssetManager`.

## Acceptance criteria

- [ ] `ReadCatalog()` enumerates all products, including unlinked Texture rows, in one read transaction and never verifies product bytes.
- [ ] The provider uses a configurable read-only database path and 50 ms default busy timeout.
- [ ] Live capture holds only `state_mutex_`, copies one coherent live epoch, and invokes no external code while locked.
- [ ] Archive/live product identity merges correctly; custom/runtime-only nodes stay generic.
- [ ] Owned children are not duplicated as Dependency edges.
- [ ] Archive-only dependency coverage is explicitly Unknown.
- [ ] Missing endpoints, failed sources, archive failures, and limits yield valid Partial snapshots with bounded diagnostics.
- [ ] Every published snapshot passes AB1.0 canonical validation and contains no pointer or retained lock.
- [ ] No load/import/archive mutation, Runtime/Editor integration, schema change, or payload parsing is added.
- [ ] Focused archive, catalog, concurrency, and existing Asset tests pass.

## Validation commands

```powershell
.\tools\kp.ps1 build ModelArchiveDatabaseTest
.\tools\kp.ps1 build AssetUnitTest
.\tools\kp.ps1 build AssetCatalogProviderTest
.\tools\kp.ps1 test Asset
git diff --check
```

Run the standard Debug build only if public-header or target-link changes reveal transitive compile fallout. AB1.1 is headless; Vulkan/OpenGL visual evidence begins with AB1.2/AB1.4.

## Handoff to AB1.2 and AB1.3

AB1.2 owns provider construction in Runtime composition, passes only the borrowed snapshot-source interface through Editor initialization, and refreshes explicitly rather than per frame. Selection survives refresh by stable key.

AB1.3 builds Dependency and Referencer adjacency from the one canonical edge table. It must display Unknown dependency coverage distinctly, and it must not load an archive-only product to fill missing edges.
