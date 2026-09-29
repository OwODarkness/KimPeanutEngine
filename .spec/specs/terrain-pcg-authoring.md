# Terrain PCG authoring and bake

- Status: proposed
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

TP1 implements optional CPU-only `TerrainCore` and `TerrainGeneration` targets,
validated immutable scalar fields, versioned recipe JSON, typed operator
registration, DAG validation/evaluation, a bounded cache and a revision-aware
worker executor. TP2+ remain unimplemented. Optional-module, Asset extension,
static mesh, logical render-source and host seams exist. The 3D host composition
and publication of generated preview assets require bounded integration work.
Normal `level.reload` does not supply that publication transaction. See the
[TP1 stage design](../../docs/terrain/.plan/TP1.md) and execution journal.

## Scope and non-goals

TP1-TP3 establish generation/authoring/preview/baking. TP4-TP7 add rain/fluvial
and wind erosion, biomes, vegetation placement and chunk/LOD packaging. TP8
proves a second geometry consumer; TP9 is optional optimization/advanced tools.
DLL plugin infrastructure, infinite gameplay streaming, voxel worlds, animated
water, physics collision and a universal editor framework are outside the
first authoring release.

## Invariants

- Generation kernels are headless; published input/result values are immutable.
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
- [ ] Preview regenerate/cancel/resize/close without stale publication or leaks.
- [ ] Debug in dedicated terrain mode with fixed recipe replay, isolated
  operators, intermediate snapshots, pause/step and per-node diagnostics.
- [ ] Bake and load native products in normal startup with authoring disabled.
- [ ] Validate drainage, erosion conservation/timestep/resolution behavior and
  finite/nonnegative state, alongside visual output.
- [ ] Validate wind/biome/scatter contracts and shared chunk edges/LOD seams.
- [ ] Demonstrate bounded mesh processing with representation-specific rules.
- [ ] Record numerical, visual and performance evidence for the accepted stages.

## Validation plan

Planning is Level 0: links, source-backed claims and `git diff --check`.
Implementation follows [the matrix](../../docs/validation_matrix.md): CPU
contracts; Asset publication/failure tests; full validation for common public
API/CMake boundaries; Vulkan Debug validation and OpenGL runtime captures for
preview; ordinary startup/module-disabled integration; lifecycle tests.

Use checked-in terrain recipes, baked startup fixtures and Runtime commands
including `capture.screenshot`/`poll`; extend host command registration as
needed. Proposed terrain commands are not callable yet. Engine runs occur
outside the sandbox on the Default desktop. Captures remain under
`save/screenshots/validation/`. Performance uses rebuilt RelWithDebInfo with
commit/tree, API, validation, viewport, camera, RT mode, residency/warmup and
sample-window provenance. Set budgets after matched baseline measurements.

## Risks and open questions

Host integration can broaden existing Engine mode orchestration; TP3 must
bound it. Preview asset publication is missing. Solver complexity and memory
grow with fields/resolution; CPU/GPU determinism differs. Heightfield wind
transport is not arbitrary mesh erosion. Dense vegetation requires an actual
batching/instancing consumer. TP4 solver selection, numeric tolerances, TP7 LOD
policy and future collision remain explicit design gates.
