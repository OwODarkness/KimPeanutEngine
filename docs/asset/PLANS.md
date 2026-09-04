# Asset Loading and Progress Plans

**Status: proposed.** This page maps the Asset architecture and the three
coordinated plans required to present truthful loading progress. Current work
belongs in [TODO.md](TODO.md); the multi-stage acceptance contract is the
[Asset Loading Progress spec](../../.spec/specs/asset-loading-progress.md).

## Architecture

The feature crosses three owners without creating a shared mutable loading
object:

```text
AssetManager load work
  -> copied AssetLoadSnapshot
  -> Runtime LoadTransactionSnapshot
  -> Editor loading presentation

Asset: identity, decode, dependency registration, Asset-stage observation
Runtime: startup policy, stage ordering, aggregation, commit/abort
Editor: read-only presentation and input/event pumping
Render/Graphics: presentation and later scene/GPU readiness
```

The detailed current Asset cache, ownership, dependency, and locking behavior
remains documented in [Asset Module Design](asset_module.md).

## Coordinated plans

- [LO1 — Asset load observation](.plan/LO1.md) defines transient operation
  identity, states, phases, immutable snapshots, dependency correlation,
  retention, and headless contract tests. It does not add UI or change Asset
  payload ownership.
- [LO2 — staged Runtime startup and Editor promotion](.plan/LO2.md) makes
  presentation available before the expensive startup transaction, aggregates
  all readiness stages, keeps event/render pumping alive, separates
  render-thread Editor presentation from future game-thread workspace behavior,
  and preserves transactional commit/abort.
- [LO3 — Editor loading presentation](.plan/LO3.md) adds a loading-only Editor
  mode that polls Runtime snapshots, renders honest determinate/indeterminate
  progress, displays failures, and enters the normal tool tree only after the
  Runtime commits readiness.

The order is intentional. LO1 supplies observable facts; LO2 gives them a
live, cross-subsystem transaction and a frame loop; LO3 renders that contract.
Implementing LO3 directly against `AssetManager` would invert ownership and
still miss Resource/GPU/level-instantiation work.

## Design decisions

- **Operation state is separate from `Asset`.** Loading begins before an Asset
  wrapper or `AssetID` exists, and a request may become a cache hit, fail,
  retry, or share work.
- **Snapshots, not callbacks, cross threads.** Producers update private mutable
  records; consumers receive copied value-only snapshots identified by a
  monotonically increasing revision.
- **Observation is opt-in and session-scoped.** Existing Asset callers keep the
  current zero-record path. An opaque shared session survives recursive/async
  work, retains exact aggregates plus bounded detail, and disappears when the
  caller and active operations release it; there is no permanent Runtime copy
  or global history.
- **Asset reports facts; Runtime reports readiness.** Asset can report its own
  queue, decode, dependency, and registration phases. Runtime alone can state
  whether shaders, environment processing, GPU resources, and level
  instantiation are ready.
- **Counts and phases precede smooth percentages.** Current loaders are
  serialized and mostly monolithic. Unknown or expanding dependency work is
  presented as indeterminate or as explicit completed/known counts; no fake
  byte-level precision is introduced.
- **Time and size are measured facts, not automatic progress weights.** Asset
  observations separate queue, source, dependency, registration, and inclusive
  elapsed time, and distinguish source-file from decoded-payload sizes. Unknown
  values remain absent; actual bytes read are deferred; parent inclusive time
  and child records are not summed in a way that double-counts work.
- **Presentation startup is not a second renderer.** The existing window,
  backend, and Editor presentation bridge are brought up in a minimal state and
  promoted transactionally to scene-ready operation.
- **Editor presentation is not editor-world behavior.** One render-thread
  presentation tick draws loading or workspace UI. Future selection, gizmo,
  play/edit, and viewport-world behavior is activated and ticked on the game
  thread through narrow command/adaptor seams.

## Reference findings

- Godot keeps threaded load-task status/progress separate from the loaded
  resource, correlates parent/subtasks, and prevents dependency aggregation
  from reporting backward progress. The separation and monotonic-display rule
  apply; its task-stealing/thread-pool machinery does not fit the current
  serialized KimPeanutEngine loaders. See
  [`resource_loader.h`](https://github.com/godotengine/godot/blob/master/core/io/resource_loader.h)
  and
  [`resource_loader.cpp`](https://github.com/godotengine/godot/blob/master/core/io/resource_loader.cpp).
- Bevy distinguishes root, direct-dependency, and recursive-dependency load
  readiness. Its loading-screen example waits for both recursive assets and
  render pipelines, supporting a Runtime aggregate above Asset state. Its ECS
  state machinery is not required here. See
  [`AssetServer`](https://docs.rs/bevy/latest/bevy/asset/struct.AssetServer.html)
  and
  [`loading_screen.rs`](https://github.com/bevyengine/bevy/blob/main/examples/showcase/loading_screen.rs).
- Unreal exposes streamable progress through a load handle rather than the
  loaded object. This supports independent operation identity, but the narrow
  API documentation does not determine KimPeanutEngine's aggregation policy.
  See
  [`FStreamableHandle::GetProgress`](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/Engine/FStreamableHandle/GetProgress?application_version=5.5).

## Rejected shapes

- Mutable progress fields on `Asset`, `AssetRegisterInfo`, or payload types.
- An Editor-owned observer that walks Asset caches or dependency vectors.
- Worker-thread callbacks into Editor/ImGui.
- Treating `LoadAsync` or the removed generic path queue as a complete startup
  scheduler.
- Declaring startup complete when Asset reaches 100% while later preparation
  or GPU work remains.
