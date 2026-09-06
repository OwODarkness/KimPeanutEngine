# Asset Module Documentation Guide

Read the repository [agent contract](../../AGENTS.md), the
[Asset architecture map](PLANS.md), the [Asset roadmap](TODO.md), and the
[detailed landed design](asset_module.md) before changing Asset loading or its
documentation.

## Boundaries

- Asset owns file identity, decoding, CPU payload lifetime, dependency edges,
  cache registration, and observation of Asset-owned load work.
- The MI1 offline importer is a separate Asset-owned tool/library boundary. It
  may use Core, foreign decoders, ImageIO, serialization, and the archive
  repository while the engine is closed, but it must not depend on
  `AssetManager`, `AssetID`, Runtime, Editor, Render, or Graphics.
- A load operation is transient execution state. It is not an `Asset`, an
  `AssetID`, persistent authoring metadata, or part of a payload.
- Runtime owns startup/level-load policy and aggregates Asset, Resource,
  Render, and Gameplay readiness into a user-facing transaction snapshot.
- Editor consumes copied read-only snapshots. It does not subscribe to worker
  callbacks, inspect `AssetManager` internals, or mutate startup state.
- Resource owns CPU artifact processing; Render/Graphics own GPU creation and
  safe retirement. Asset progress must not claim those stages are complete.
- Asset cost observations distinguish source, decoded payload, Resource
  artifact, and GPU sizes. Unknown measurements remain absent; estimates must
  be labeled and cannot masquerade as measured values.
- Preserve the current lock order (`load_mutex_` before `state_mutex_`). Load
  observation must use independent synchronization and must never invoke
  external code while an Asset lock is held.
- LO1 observation is opt-in and session-scoped. Keep exact aggregates but bound
  copied active/terminal detail; do not add permanent operation history to
  Runtime or a global Asset observer registry.

## Documentation layout

- `PLANS.md` maps architecture and the coordinated load-progress stages.
- `asset_module.md` describes the detailed landed Asset implementation.
- `TODO.md` is the acceptance-oriented roadmap.
- `.plan/LO*.md` contains the coordinated load-progress stage designs.
- `.plan/MI*.md` contains model-import stage designs.
- `.spec/specs/asset-loading-progress.md` owns the cross-stage objective,
  invariants, and final acceptance contract.
- `.spec/journal/` records implementation and validation evidence only after
  work occurs.

Do not duplicate execution history into the plans or roadmap.

When assigned an exact model-import ID such as `MI1.3`, read the parent
[MI1 architecture](.plan/MI1.md) and that stage's page, implement only its
assignment and deliverables, and respect its prerequisite and boundary lists.
Do not absorb adjacent `MI1.x` stages without an explicit reassignment. Record
execution evidence in the MI1 journal and mark the matching
[TODO entry](TODO.md#model-import-roadmap) complete only after its `Done when`
checks pass.

## Validation

Follow the [validation matrix](../validation_matrix.md). Asset observation
changes require focused headless Asset tests. Startup lifecycle changes require
Runtime startup tests. Editor presentation changes require Editor build/startup
coverage, Vulkan and OpenGL smoke, and visual evidence of loading, success, and
failure states.
