# Asset Catalog Contract — AB1.0

- Date: 2026-09-13
- Plan: [AB1.0](../../docs/asset/asset-browser/.plan/AB1.0.md)
- Parent design: [Asset Browser Plans](../../docs/asset/asset-browser/PLANS.md)
- Scope: value-only catalog contract, stable-key helpers, canonicalization,
  validation, and a reusable synthetic fixture

## Result

AB1.0 froze the catalog contract that AB1.1 will populate and AB1.2/AB1.3 will
present. Nothing in this stage enumerates the archive, reads `AssetManager`, or
touches SQLite, ImGui, Render, or Graphics.

`asset_catalog.h` declares the snapshot values, the `IAssetCatalogSnapshotSource`
provider boundary, the four stable-key builders, and the two pure helpers.
`asset_catalog.cpp` implements keys, canonicalization, and validation. Both are
free of filesystem, database, and lock access; the only Asset state they read is
the value they are handed.

### Semantics settled by this stage

- **Node identity split.** `AssetCatalogNodeId` is a dense index used only for
  compact lookup inside one snapshot. `stable_key` is the opaque,
  refresh-stable selection identity that only Asset constructs or interprets.
- **Stable-key namespaces.** Four frozen V1 spellings under the
  `asset-catalog-v1/` prefix: `product/<type>/<hash>`,
  `path/<type>/<canonical-path>`, `runtime/<packed-id-hex>`, and
  `missing/<sha256-owner>/<relation>/<ordinal>/<type>`. Decimal fields carry no
  leading zeroes; the owner key is hashed so arbitrary owner text cannot forge a
  delimiter. `MakeRuntimePathCatalogKey` canonicalizes its input, so the key
  cannot disagree with `AssetManager`'s path index.
- **One forward edge table.** `Dependency` and `OwnedChild` are the only
  relations. An ID present in both `dependencies` and `owned_children` yields
  exactly one `OwnedChild` edge. `ordinal` is the original zero-based position
  within one `(from, relation)` sequence; `label` is readable context that
  identity and ordering never depend on.
- **Canonical order.** Nodes sort by stable key bytewise, then dense IDs are
  assigned, then edge endpoints are remapped. Edges sort by
  `(from, relation, ordinal, target key)`, diagnostics by
  `(severity, code, related key, message)`, and per-node aliases and provenance
  are normalized and ordered. No locale-aware comparison is used.
- **Partial failure.** `CaptureAssetCatalog` must return a structurally valid
  snapshot even when archive access fails. A `Complete` snapshot carrying an
  `Error` diagnostic is rejected; a `Partial` one is accepted. A missing target
  is a `MissingReference` leaf with `Missing` availability and a matching
  `UnresolvedDependency` diagnostic, never a dangling endpoint or an edge
  source.

### Validation rules frozen beyond the plan text

The plan named the invariants but left the availability/metadata mapping open.
This stage fixed it as follows so AB1.1 does not re-decide it:

| Availability | `archive_product_type` | `content_hash` | `packed_runtime_asset_id` | Coverage |
| --- | --- | --- | --- | --- |
| `ArchiveOnly` | required | required | forbidden | `Unknown` |
| `LoadedArchiveProduct` | required | optional | optional | `Complete` |
| `RuntimeOnly` | forbidden | optional | optional | either |
| `Missing` | forbidden | forbidden | forbidden | `Unknown` |

`content_hash` stays optional for `LoadedArchiveProduct` so a partial snapshot
captured while the archive is unreadable can still publish the live graph.
`product_path` is required for both archive availabilities. `MissingReference`
implies `Missing` and vice versa. Ordinary assets require a concrete
`AssetType` and a non-empty `type_name`; missing references may carry neither.

`CanonicalizeAssetCatalogSnapshot` works on a copy and publishes only on
success, so a rejected snapshot is left byte-identical. Its input must already
carry dense node IDs because edge endpoints are interpreted as indices into
`nodes`.

## Files

- `engine/runtime/asset/asset_catalog.h` (new)
- `engine/runtime/asset/asset_catalog.cpp` (new)
- `engine/runtime/asset/CMakeLists.txt`
- `engine/test/support/asset_catalog_fixture.h` (new)
- `engine/test/unit/asset/asset_catalog_contract_test.cpp` (new)
- `engine/test/unit/asset/CMakeLists.txt`

## Validation

- `tools/kp.ps1 build AssetUnitTest` — passed. `AssetRuntime` and
  `AssetUnitTest` rebuilt with no warnings from the new translation unit.
- `tools/kp.ps1 test Asset` — passed, 85/85.
- `AssetUnitTest --gtest_filter=AssetCatalogContractTest.*` — passed, 21/21,
  covering golden keys, dense IDs, order independence, endpoint remapping,
  cycles, shared targets, owned-child non-duplication, coverage combinations,
  missing-reference rules, provenance ordering, custom types, `Complete` versus
  `Partial`, and copy independence.
- `git diff --check` — clean.

Runtime rendering evidence is not applicable: AB1.0 adds no runtime, resource,
or GPU behavior. A full engine build was not run because the change adds a new
file and adds one source to an existing static library; no existing header was
modified.

## Remaining work

AB1.1 owns archive enumeration, the bounded live-cache copy under
`state_mutex_`, the archive/live join, alias retention, reverse-edge proof, and
headless concurrency tests. It may add diagnostics but may not renumber existing
codes or change golden key spelling without a catalog-contract version increment.
AB1.2 and AB1.3 own all Editor presentation; AB1.0 deliberately added no
traversal depth or row limits, which belong to the AB1.3 bounded projection.
