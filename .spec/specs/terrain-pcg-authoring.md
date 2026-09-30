# Terrain PCG authoring and bake

- Status: active
- Owner: engine terrain module; planning by Amadeus
- Parent TODO: [terrain roadmap](../../docs/terrain/TODO.md)
- Architecture: [terrain plans](../../docs/terrain/PLANS.md)
- Investigation: [planning journal](../journal/2026-09-29-terrain-pcg-plan.md)

## Objective

Create an optional, extensible terrain authoring module with a headless CPU
evaluator, dedicated viewer/workspace, modern landform/erosion operators, and
reproducible native engine bakes. Initial geometry is a finite heightfield;
future mesh processing must fit typed capabilities without hiding projection loss.

## Current state

TP1 implements optional `TerrainCore` and `TerrainGeneration` targets,
validated immutable scalar fields, versioned recipe JSON, typed operator
registration, DAG validation/evaluation, a bounded cache and a revision-aware
worker executor. TP2 adds deterministic heightfield controls, mesh/raster
projection, derivatives, priority-flood drainage and the shared-renderer
`terrain-viewer`. TP3 adds seeded authoring controls, field diagnostics,
generation cancel/step commands, catalog replacement, native model/material
baking and recipe provenance. Vulkan and OpenGL now visibly render the preview;
fresh normal Scene3D startup loads the bake on both APIs. Successful replacement,
cancel retention, resize and post-swap screenshots were exercised. Invalid
generation and archive-commit failures preserve prior state; catalog-promotion
failure injection and high-count replacement stress remain open.
See the [TP1 stage design](../../docs/terrain/.plan/TP1.md),
[TP2 stage design](../../docs/terrain/.plan/TP2.md),
[TP3 stage design](../../docs/terrain/.plan/TP3.md) and the
[TP3 execution journal](../journal/2026-09-30-terrain-pcg-TP3.md).

## Scope and non-goals

TP1-TP3 establish generation/authoring/preview/baking. TP4-TP7 add rain/fluvial
and wind erosion, biomes, vegetation placement and chunk/LOD packaging. TP8
proves a second geometry consumer; TP9 is optional optimization/advanced tools.
DLL plugin infrastructure, infinite gameplay streaming, voxel worlds, animated
water, physics collision and a universal editor framework are outside the
first authoring release.

## Invariants

- Generation kernels are headless; published input/result values are immutable.
- PCG evaluation has no Gameplay/Render dependency; the viewer/runtime adapter
  reuses Gameplay Actor/components and RenderSystem for result presentation.
- Asset/Resource/Render/Graphics ownership and backend neutrality are preserved.
- Runtime gains no terrain UI dependency; ordinary baked models need no generator.
- Dedicated `terrain-viewer` is the first interactive integration; its preview
  scene/session is isolated from game Level startup and uses shared 3D rendering.
- Ordinary Scene3D startup does not construct terrain authoring services.
- Typed ports reject unsupported geometry; global solvers declare global scope.
- Units, domain, seed, operator versions, boundaries and quality are explicit.
- Superseded jobs cannot replace current preview; failure preserves valid output.
- Bakes stage/validate/publish before archive commit and retain previous output
  on failure. GPU resources retire only after submitted use is safe.

## Stages

Canonical scope, dependencies and acceptance live in
[TP0-TP9](../../docs/terrain/TODO.md). Add concrete `.plan/TP*.md` designs when
the relevant implementation starts; do not duplicate the roadmap here.

## Acceptance criteria

- [x] TP1: serialize/reopen recipe JSON, produce repeatable same-profile CPU
  results, and register an independent typed operator without evaluator edits.
- [x] TP2 CPU: landform and derived-field fixture contracts, physical sampling,
  projection loss/fallback, acyclic drainage and accumulation conservation.
- [x] TP2 view mode: fixed shared-renderer terrain preview without gameplay Level
  instantiation or Editor setup; Vulkan and OpenGL captures are visible.
- [x] Generate deterministic previews from the checked-in 128x128 fixture;
  inspect intermediate fields and per-node finite/range/time/memory/hash data;
  expose regenerate/cancel/pause/step/resume in the authoring session.
- [x] Replace the preview at a Render frame boundary, reject stale worker
  revisions, preserve the committed preview on cancel, and wait for submitted
  Render work during scene resource teardown.
- [x] Bake a native model/material plus content-addressed recipe provenance,
  then load the logical model in fresh normal Scene3D startup with authoring
  disabled on Vulkan and OpenGL.
- [x] Capture Debug Vulkan and OpenGL output, world normals and material;
  exercise OpenGL resize and clean viewer shutdown. Terrain currently uses one
  mesh, so chunk-edge checks belong to TP7.
- [x] Reject invalid generation controls without replacing the last preview;
  reject an archive commit under a SQLite lock and reload the previous bake.
- [ ] Inject prepared-catalog promotion failure and verify preview rollback.
  Stress high-count replacement and resource retirement under repeated swaps.

## Validation plan

Planning is Level 0: links, source-backed claims and `git diff --check`.
Implementation follows [the matrix](../../docs/validation_matrix.md): CPU
contracts; Asset publication/failure tests; full validation for common public
API/CMake boundaries; Debug Vulkan validation and OpenGL runtime captures for
preview; ordinary startup and lifecycle checks. The TP3 journal records each
run and leaves failure injection open until it is exercised.

Use checked-in terrain recipes, baked startup fixtures and Runtime commands
including `capture.screenshot`/`poll`, `terrain.regenerate`, `terrain.cancel`,
`terrain.execution_control`, and `terrain.bake`. Engine runs occur outside the
sandbox on the Default desktop. Captures remain under
`save/screenshots/validation/`. Performance uses rebuilt RelWithDebInfo with
commit/tree, API, validation, viewport, camera, RT mode, residency/warmup and
sample-window provenance. Set budgets after matched baseline measurements.

## Risks and open questions

The baseline catalog replacement rebuilds scene-owned Render state and waits
for submitted work; catalog-promotion rollback injection and high-count
replacement stress remain unverified. Solver complexity and memory grow with fields/resolution; CPU/GPU
determinism differs. Heightfield wind transport is not arbitrary mesh erosion.
Dense vegetation requires an actual batching/instancing consumer. TP4 solver
selection, numeric tolerances, TP7 LOD policy and future collision remain
explicit design gates.
