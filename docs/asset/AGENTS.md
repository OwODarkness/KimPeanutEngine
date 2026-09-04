# Asset Module Documentation Guide

Read the repository [agent contract](../../AGENTS.md), the
[Asset architecture map](PLANS.md), the [Asset roadmap](TODO.md), and the
[detailed landed design](asset_module.md) before changing Asset loading or its
documentation.

## Boundaries

- Asset owns file identity, decoding, CPU payload lifetime, dependency edges,
  cache registration, and observation of Asset-owned load work.
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
- `.plan/LO*.md` contains the concrete stage designs.
- `.spec/specs/asset-loading-progress.md` owns the cross-stage objective,
  invariants, and final acceptance contract.
- `.spec/journal/` records implementation and validation evidence only after
  work occurs.

Do not duplicate execution history into the plans or roadmap.

## Validation

Follow the [validation matrix](../validation_matrix.md). Asset observation
changes require focused headless Asset tests. Startup lifecycle changes require
Runtime startup tests. Editor presentation changes require Editor build/startup
coverage, Vulkan and OpenGL smoke, and visual evidence of loading, success, and
failure states.
