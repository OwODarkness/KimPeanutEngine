# MI1.1 — Contract Characterization

- Status: landed 2026-09-06
- Parent design: [MI1](MI1.md)
- Prerequisites: none
- Unblocks: MI1.2 and MI1.3

## Assignment

Freeze the observable behavior of the current Assimp-backed loader before its
responsibilities are split. Add small checked-in fixtures and focused tests for
OBJ, FBX, GLTF, GLB, and failure cases; add STL characterization only after its
current unsupported dispatch is recorded.

## Deliverables

- Record vertex/index counts, section ranges, material-slot indices, baked node
  transforms, bounds, material metadata, and source-relative texture paths.
- Cover malformed input, missing companion files, unsupported extension, and
  path-resolution diagnostics without changing production architecture.
- Name fixture provenance and keep binary fixtures minimal and redistributable.
- Create the MI1 spec and journal skeleton required by the parent plan, without
  claiming implementation results in advance.

## Fixture inventory

The characterization fixtures live under
`engine/test/unit/asset/fixtures/mi1_1/`:

- `obj/triangle.obj` + `triangle.mtl` + `textures/albedo.ppm` — three indexed
  vertices, one section, Assimp's default material slot 0 plus the authored
  material at slot 1, and an OBJ-relative texture reference.
- `fbx/triangle.fbx` — minimal hand-authored ASCII FBX triangle.
- `gltf/transform.gltf` — GLTF 2.0 with an embedded vertex/index buffer and a
  translated node/material.
- `glb/triangle.glb.b64` — the same minimal GLB container stored as reviewable
  base64 text and materialized by the test before loading.
- `failure/malformed.gltf`, `failure/missing_buffer.gltf`, and
  `failure/unsupported.stl` — malformed, missing-companion, and unsupported
  STL baselines.

The current runtime keeps Assimp's detailed parser error in the log while the
Asset observation exposes the stable filename/phase/rejection diagnostic. The
STL fixture is intentionally not dispatched by the current `AssetManager`;
support belongs to MI1.3 and later import stages.

## Boundaries

Do not add SQLite, native serialization, a new decoder abstraction, or change
runtime load behavior. A bug discovered here is documented as a deliberate
future correction unless a test cannot be written without the smallest fix.

## Done when

- [x] Focused tests deterministically describe the existing foreign-loader
  contract and pass on supported build configurations.
- [x] The unsupported STL baseline and all intentional gaps are explicit.
- [x] The journal records fixture sources, commands, results, and skipped paths.
