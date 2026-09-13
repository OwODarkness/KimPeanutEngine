# Asset Catalog Provider — AB1.1

- Date: 2026-09-13
- Plan: [AB1.1](../../docs/asset/asset-browser/.plan/AB1.1.md)
- Parent design: [Asset Browser Plans](../../docs/asset/asset-browser/PLANS.md)
- Prerequisite: [AB1.0 journal](2026-09-13-asset-catalog-contract.md)
- Scope: strict read-only archive enumeration, bounded live capture under the
  Asset state lock, and the merge into one canonical snapshot

## Result

AB1.1 populated the contract AB1.0 froze. Three new Runtime sources now produce
a valid `AssetCatalogSnapshot` from real SQLite and real `AssetManager` state,
and nothing else in the engine changed.

`model_archive.{h,cpp}` gained `ReadCatalog()` — one read transaction that
enumerates sources, their dependency and product links, the products table
(including rows with no source link), material overrides, and the optional
dependency metadata, exposed as `ModelArchiveCatalogSnapshot` plus configurable
`ModelArchiveCatalogReadLimits`. It verifies no product bytes and opens no
payload.

`detail/asset_catalog_builder.{h,cpp}` is a pure builder: it takes the archive
snapshot, a value-only copy of live manager records, type names, limits, and a
revision, and returns the canonical graph. It touches no database, no manager,
and no lock, which is why the whole join and ordering matrix is covered by fast
tests with no I/O.

`asset_catalog_snapshot_provider.{h,cpp}` implements
`IAssetCatalogSnapshotSource` in two disjoint phases: the read-only archive read
with no Asset lock held, then a bounded copy of manager state under
`state_mutex_`. It releases the lock before assembly, so no lock and no manager
reference outlives the capture.

AB1.2 and AB1.3 can now display archive and live nodes and derive both graph
directions from the frozen contract alone.

### Semantics settled by this stage

- **Capture is two disjoint phases.** Phase 1 opens `ModelArchiveDatabase` in
  `ReadOnly` with the configured busy timeout and reads the catalog without
  holding `state_mutex_`. Phase 2 takes `std::lock_guard` on `state_mutex_`,
  copies live records, and releases. Assembly runs on the copies. An
  `InvalidArgument` from the reader is reported as `CaptureLimitExceeded`;
  every other archive error becomes `ArchiveUnavailable` and the capture still
  publishes the live half as `Partial`.
- **Manager storage stays private.** The only `AssetManager` change is a
  forward declaration and `friend class AssetCatalogSnapshotProvider;`. No
  general cache-enumeration API was added, so the cache policy remains the
  manager's to change.
- **Identity join order.** Archive nodes are laid down first because they own
  product identity. A live record merges into an archive node when its product
  path key and type match; otherwise it appends a `RuntimeOnly` node keyed by
  its path key or packed id.
- **Only the first live record per product path merges.** Two live records that
  share one product path produce one node and one diagnostic, not two merged
  identities.
- **Overflow is asymmetric on purpose.** Nodes, edges, diagnostics, and
  per-node aliases each have a limit. When the node limit is hit mid-join the
  live portion is dropped and the archive portion is kept, because the archive
  half is the stable inventory an operator can still act on. If the archive
  snapshot alone already exceeds the limit, no archive portion is published at
  all and the snapshot is `Partial`.
- **Diagnostics have a required bucket.** Contract-required entries — archive
  failure, capture-limit, assembly failure — are never truncated. Optional
  entries fill the remaining space, and one slot is reserved so a truncated set
  can still say it was truncated. Warnings may accompany a `Partial` snapshot;
  any `Error` forces `Partial` even when everything else succeeded.
- **A self-owned child is dropped, not fatal.** A child that names its own owner
  is removed with an `InvalidOwnedChildLayout` diagnostic. Failing the whole
  snapshot over one malformed record would be worse than publishing the rest.
- **Owner alias selection is order-independent.** The name shown for a root
  Model is the lexicographically smallest source display name among the sources
  that import it. Several sources can import the same root, and no SQLite row
  order is an identity, so first-seen would have been nondeterministic.
- **Two path spellings.** `product_path` is archive-root-relative (as stored);
  `logical_path` is project-relative with no `.archive/` segment. They are
  different namespaces and are never compared to each other.

### Defect found and fixed during this stage

`CatalogAssembler` never copied `input.live` into its working vector, so every
live record was silently absent from every snapshot. The pure-builder test
matrix caught it on first run — 18 of 25 new cases failed with the live half
missing. Fixed by initializing the member from the input; no other logic
changed. This is the concrete payoff of keeping the builder free of I/O.

## Files

- `engine/runtime/asset/model_archive.h`, `model_archive.cpp` — `ReadCatalog()`,
  `ModelArchiveCatalogSnapshot`, `ModelArchiveCatalogReadLimits`
- `engine/runtime/asset/asset_catalog_snapshot_provider.h`, `.cpp` (new)
- `engine/runtime/asset/detail/asset_catalog_builder.h` (new)
- `engine/runtime/asset/detail/asset_catalog_builder.cpp` (new)
- `engine/runtime/asset/CMakeLists.txt` — three sources added to `AssetRuntime`
- `engine/runtime/asset/asset_manager.h` — friend declaration only
- `engine/test/unit/asset/asset_catalog_builder_test.cpp` (new, 25 tests)
- `engine/test/unit/asset/asset_catalog_provider_test.cpp` (new, 8 tests)
- `engine/test/unit/asset/model_archive_database_test.cpp` — eight catalog tests
- `engine/test/unit/asset/CMakeLists.txt` — builder source added to
  `AssetUnitTest`; new `AssetCatalogProviderTest` executable

`AssetCatalogProviderTest` is a separate executable because the provider reads
the process-wide `AssetManager` singleton and must not share a type registry or
cache with the other Asset tests.

## Validation

- `cmake --build build --config Debug --target ModelArchiveDatabaseTest
  AssetUnitTest AssetCatalogProviderTest` — passed. `AssetRuntime` rebuilt with
  no warnings from the new translation units.
- `ModelArchiveDatabaseTest` — 14/14 passed, including the eight catalog cases
  for strict read-only enumeration, global-product ordering, shared products
  and failed sources, configured limits, corrupt rows and broken links,
  read-only/read-write parity, absent dependency metadata, and one committed
  epoch under a concurrent writer.
- `AssetUnitTest` — 97 of 99 passed. All 25 new `AssetCatalogBuilderTest` cases
  pass. The two failures are pre-existing and unrelated:
  `AssetContractBaselineTest.RegisteredCustomTypeLoadsThroughAssetCache` (a
  singleton type-registry ordering artifact; it passes under CTest, where each
  case is its own process) and
  `LevelLoaderTest.LoadsCheckedInGameplayLevelFixtures` (the checked-in
  `pbr_showcase.level` references `model/rock1-bl/rock2`, which is absent from
  the repository archive). Both were confirmed non-regressions by re-running
  with `--gtest_filter='-AssetCatalogBuilderTest.*'`.
- `AssetCatalogProviderTest` — 8/8 passed: missing archive, unreadable archive
  file, default path resolution, live copy with no payload data crossing the
  boundary, revision monotonicity, no manager mutation during capture,
  concurrent mutation and lookup, and provider destruction.
- `ctest --test-dir build -C Debug -R
  "Asset|Model|Material|Texture|Level|Import"` — 276 of 277 passed. The single
  failure is `LevelLoaderTest.LoadsCheckedInGameplayLevelFixtures`, the
  pre-existing missing-fixture case above.
- `git diff --check` — clean.

Runtime rendering evidence is not applicable: AB1.1 is headless and adds no
runtime, resource, or GPU behavior. Vulkan and OpenGL visual evidence begins
with AB1.2/AB1.4.

## Remaining work

AB1.2 owns provider construction in Runtime composition, passing only the
borrowed `IAssetCatalogSnapshotSource` interface through Editor initialization,
and explicit refresh rather than per-frame capture. AB1.3 owns both graph
directions from the one canonical edge table. Neither may renumber diagnostic
codes or change golden key spelling without a catalog-contract version
increment.

Not yet exercised end to end: capture against the checked-in repository archive
at scale. The focused tests use small synthetic archives and a temporary
manager; the largest real archive is only reachable once AB1.2 wires the
provider into Runtime composition.
