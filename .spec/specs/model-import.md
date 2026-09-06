# MI1 — Native Model Import

Status: active. This spec owns the staged model-import work described by
[`docs/asset/.plan/MI1.md`](../../docs/asset/.plan/MI1.md).

## Objective

Convert foreign STL, OBJ, FBX, GLTF, and GLB sources into immutable,
content-addressed native `.model` products with generated `.material`
products, while keeping runtime loading read-only and independent of the
authoring SQLite archive.

## Scope and stages

1. MI1.1 characterizes the existing Assimp contract with checked-in fixtures.
2. MI1.2 supplies stable hashing and the SQLite archive repository.
3. MI1.3 extracts a pure foreign-model decoder.
4. MI1.4 defines and validates native Model V1.
5. MI1.5 converts source materials and images.
6. MI1.6 implements transactional import and publication.
7. MI1.7 migrates runtime loading to native `.model` products.
8. MI1.8 adds explicit tooling and end-to-end validation.

The stages are ordered by the dependency graph in the parent plan. MI1.1 does
not change production loading behavior, add SQLite, serialize native products,
or introduce a decoder abstraction.

## Invariants

- Asset owns runtime identity, dependency registration, and CPU payload life.
- The offline importer is a separate Asset-owned tool/library boundary. It may
  run while the engine application is closed and must not depend on
  `AssetManager`, `AssetID`, Runtime, Editor, Render, or Graphics.
- Assimp remains a foreign-source decoder and does not write products or update
  the archive.
- Native products are versioned, immutable, bounds-checked, and named by the
  hash of their canonical bytes.
- Source package fingerprints include the root source, discovered dependency
  paths and bytes, importer/settings identity, and schema versions.
- Runtime `.model` loading does not open SQLite, invoke Assimp, or write
  project content.
- A failed import leaves the last committed source root usable.

## Acceptance

The full acceptance ledger remains in
[`docs/asset/.plan/MI1.md`](../../docs/asset/.plan/MI1.md). MI1.1 specifically
requires deterministic characterization of OBJ, FBX, GLTF, and GLB geometry,
sections, material slots, transforms, bounds, metadata, and relative texture
paths, plus malformed, missing-companion, unsupported-STL, and diagnostic
coverage. STL remains explicitly unsupported by the current runtime dispatch
until MI1.3.

## Validation

The focused evidence target is `AssetUnitTest`, especially the
`AssimpModelLoaderTest.*` cases. Broader native-format, archive, runtime, and
cross-backend validation belongs to later MI1 stages.
