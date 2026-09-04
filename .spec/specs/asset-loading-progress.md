# Asset Loading Progress Screen

- Status: active
- Owner: project team
- Parent TODO: [Asset Loading Progress TODO](../../docs/asset/TODO.md)

## Objective

Present a truthful, responsive loading-progress screen from the start of the
expensive startup transaction until the selected level is fully ready, while
preserving Asset, Runtime, Editor, Render, and Graphics ownership boundaries.

## Current state

Asset loading is synchronous at startup and exposes only an eventual `AssetID`.
Recursive dependencies are loaded and registered by `AssetManager`, but there
is no operation-state observation. Runtime then prepares shaders and environment
artifacts before starting the render thread. Window, backend, ImGui, and Editor
UI initialization happen only afterward, so no progress UI can currently be
presented during the expensive work.

## Scope and non-goals

In scope:

- Asset-owned transient load-operation observation, immutable snapshots, and
  per-operation time/size costs.
- A Runtime-owned aggregate startup transaction and staged presentation/scene
  lifecycle.
- An Editor loading-only mode that polls and displays copied Runtime state.
- Transactional success, failure, close/cancel, and partial-startup teardown.
- Headless contract tests plus Vulkan/OpenGL runtime and visual evidence.

Out of scope:

- Loader-pool parallelism or general streaming architecture.
- Live level switching, unload progress, cooperative cancellation inside every
  decoder, or shipping-game loading-screen customization.
- Changing Asset identity, payload ownership, dependency semantics, or GPU
  ownership.

## Invariants

- Load operation identity is distinct from `AssetID`; observation data is not
  stored in `Asset`, `AssetRegisterInfo`, or payloads.
- Asset observation is opt-in and session-scoped. Session state is shared only
  with active operations, retains exact aggregates plus bounded detail, and is
  released without a global registry or permanent Runtime history.
- Asset reports only Asset-owned work. Runtime is the sole authority for
  complete startup readiness.
- Asset timing uses a monotonic clock and separates queue, source, dependency,
  registration, and inclusive elapsed costs. Inclusive parent and child times
  must not be summed as exclusive work.
- Source-file and decoded-payload bytes are distinct optional measurements.
  Unknown values are not represented as zero; actual bytes read remain deferred
  until loaders can report them; Asset does not report Resource artifacts or
  GPU allocation as Asset byte cost.
- Editor consumes copied value-only snapshots and never receives worker-thread
  callbacks or mutable subsystem storage.
- ImGui and backend presentation remain render-thread-owned; Asset/CPU
  preparation does not move to that thread merely for progress reporting.
- The existing Asset lock order is preserved. Observation publication cannot
  call external code under Asset locks.
- One window, graphics backend, and Editor presentation implementation are used
  through loading and normal rendering.
- Scene-dependent APIs are unavailable before scene-ready promotion.
- Startup either commits fully once or rolls back fully; GPU resources are
  released only when submitted work is safe.

## Reference gate

- Godot's `ThreadLoadTask` keeps status/progress and task hierarchy outside the
  resource object. Its dependency aggregation maintains a maximum reported
  progress. Applicable: operation/resource separation, parent correlation,
  monotonic presentation. Not adopted: its thread-pool/task-stealing model.
  Sources:
  [`resource_loader.h`](https://github.com/godotengine/godot/blob/master/core/io/resource_loader.h),
  [`resource_loader.cpp`](https://github.com/godotengine/godot/blob/master/core/io/resource_loader.cpp).
- Bevy exposes root, direct-dependency, and recursive-dependency states, while
  its loading-screen example also waits for render-pipeline readiness.
  Applicable: distinguish Asset closure readiness from whole-scene readiness.
  Not adopted: ECS state scheduling as the implementation mechanism. Sources:
  [`AssetServer`](https://docs.rs/bevy/latest/bevy/asset/struct.AssetServer.html),
  [`loading_screen.rs`](https://github.com/bevyengine/bevy/blob/main/examples/showcase/loading_screen.rs).
- Unreal's streamable handle owns a queryable progress value independently of
  the loaded object. Applicable: operation-scoped progress identity. Its public
  API page is insufficient evidence for dependency weighting or startup-loop
  structure, so those decisions remain local. Source:
  [`FStreamableHandle::GetProgress`](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/Engine/FStreamableHandle/GetProgress?application_version=5.5).

## Stages

1. [LO1 — Asset load observation](../../docs/asset/.plan/LO1.md): operation
   records, state/phase facts, time/size cost, correlation, immutable snapshots,
   and tests.
2. [LO2 — staged Runtime startup loop](../../docs/asset/.plan/LO2.md): minimal
   presentation, aggregate transaction, scene promotion, commit/abort, and
   responsiveness.
3. [LO3 — Editor loading presentation](../../docs/asset/.plan/LO3.md): loading
   view model/component, loading-to-ready transition, and visual evidence.

## Acceptance criteria

- [x] The startup root and recursive Asset operations can be observed through
  coherent value snapshots from queue/cache lookup to terminal state.
- [x] Terminal Asset observations contain stable monotonic timing attribution
  and optional source/decoded byte costs; active elapsed values advance, cache
  hits cause no measurement-only file query, and dependency aggregation does
  not double-count inclusive parent/child work.
- [ ] Runtime can poll an exact aggregate and bounded operation detail without
  retaining or replaying the complete Asset operation history.
- [x] Asset failure includes a stable operation/path/stage diagnostic without
  creating a partial parent Asset.
- [ ] The window appears and continues polling events and presenting frames
  while a deterministic startup gate holds Asset or CPU-preparation work.
- [ ] The Runtime snapshot distinguishes Asset loading, CPU preparation, scene
  GPU promotion, level instantiation, Ready, and failure.
- [ ] Editor displays exact counts and an honest determinate/indeterminate state
  without reading AssetManager, Asset payloads, or scene-only Render APIs.
- [ ] The loading-to-ready transition occurs exactly once and the first normal
  frame uses the committed level and prepared catalog.
- [ ] Closing or failing during every startup stage wakes waiters and releases
  acquired state in reverse order without leaking or accessing destroyed state.
- [ ] Focused tests, full Debug build/CTest, Vulkan/OpenGL smoke, and inspected
  loading/ready visual captures pass.

## Validation plan

Follow [the validation matrix](../../docs/validation_matrix.md). The final
feature crosses Asset, Runtime, Render, Graphics, and Editor, so it requires
full validation rather than compilation-only evidence. Use injected gates and
failure hooks for deterministic tests; never add production sleeps solely to
make a loading screen capturable.

## Risks and open questions

- The current RenderSystem requires a complete prepared catalog to initialize;
  splitting presentation-ready from scene-ready is the largest lifecycle risk.
- Scene/GPU promotion may still block the render thread. Measure it and make
  only the work that violates the responsiveness bound incremental.
- Dependency totals are discovered dynamically. Exact counts can expand, so
  monotonic UI smoothing must not overwrite the factual snapshot.
- Retained payload size can be calculated cheaply for known types, but current
  allocators do not expose trustworthy transient peak usage. Peak memory stays
  absent until measured or explicitly labeled as an estimate.
- The current RuntimeLib/EditorLib dependency cycle is known. The snapshot
  source must use the narrow existing composition seam and must not add a new
  Editor dependency to Runtime implementation code.
