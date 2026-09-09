# Asset Module Plans

**Status: active.** This page maps the Asset architecture and links its concrete
stage plans. Current work belongs in [TODO.md](TODO.md). Detailed landed
behavior remains in [asset_module.md](asset_module.md).

## Loading-progress architecture

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

## Loading-progress plans

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

## Startup performance architecture

The loading-progress work explains what startup is doing; it does not make the
dependency closure smaller or faster. The measured Sponza startup instead
requires coordinated product, package, scheduling, and residency changes:

```text
Offline Asset import/cook
  -> compact native Model and GPU-compressed Texture products
  -> dependency-closure package with independently addressable entries/ranges

Runtime startup policy
  -> Asset dependency jobs with in-flight deduplication
  -> verified low-mip and metadata readiness
  -> initial scene commit
  -> background high-mip completion

Asset: product identity, verification, dependency scheduling, CPU payloads
Runtime: startup priority and initial/full readiness policy
Resource: CPU-to-GPU artifact preparation
Render/Graphics: mip usability, upload, synchronization, GPU lifetime
```

[AP1 — Startup Asset Loading Performance](.plan/AP1.md) owns this design. It
starts with reproducible attribution and removal of redundant product hashing
and integrity copies,
then adds GPU-native Texture compression, compact Model products, package
locality, bounded dependency scheduling, and low-mip initial readiness. The
literal embedding of Texture bytes in Material products is rejected; packages
preserve independent content identities while colocating related entries.

The multi-session acceptance contract is
[Asset Startup Loading Performance](../../.spec/specs/asset-startup-loading-performance.md).

## Model-import architecture

```text
Offline importer tool (engine may be closed)
  -> source-path hash + SQLite archive lookup
  -> verify root + recorded dependency fingerprint
  -> Assimp decode and rediscovery only on cache miss
  -> Asset-owned imported document
  -> explicit import/reimport transaction
  -> hash-named native .model + .material products
  -> atomic SQLite name/hash/dependency transaction

Runtime AssetManager (read-only product consumer)
  -> read-only archive lookup for readable Level model keys
  -> native Model load and dependency registration
  -> Model material-slot table
  -> Gameplay/Render consumption with authored overrides
```

Texture products use the same boundary without requiring runtime Asset state:

```text
TextureImporter (decode source, database-free)
  -> TextureCooker (semantic mips + bounded portable format)
  -> immutable .archive/textures/<hash>.texture
  -> NativeTextureLoader (read-only runtime adapter)
```

`TextureImporter` and `TextureCooker` are separate stages. The importer owns
source decoding only; the cooker owns semantic filtering, dimension bounds,
portable format selection, and canonical native serialization. Neither stage
constructs an `Asset`, opens the runtime manager, or creates a GPU object.

Assimp owns foreign source-format decoding only. The standalone Asset import
tool/library owns source closure discovery, hash/no-op decisions, native
serialization, immutable content-addressed products, staging, and short SQLite
metadata transactions. It must run without `AssetManager`, `AssetID`, Runtime,
Editor, Render, or Graphics, so import/reimport remains available while the
engine application is closed. Runtime Level loading may open the archive in a
strict read-only mode to resolve a readable logical model key to its verified
hash-named `.model` product. It never imports, writes the archive, or decodes a
foreign source at runtime. The native Model loader itself remains database-free.
Render consumes ordinary Material AssetIDs and never imports files or owns
source-material metadata.

## Model-import plans

- [MI1 — content-addressed native model import](.plan/MI1.md) defines foreign
  STL/OBJ/FBX/GLTF/GLB source import, dependency-aware fingerprints, the native
  `.model` format, hash-named `.model`/`.material` products, the
  SQLite name-to-hash/dependency archive, no-op reuse, Model material
  references, transactional publication, runtime migration, and validation.
  Its implementation is split into independently assignable
  [MI1.1 through MI1.8 stage contracts](.plan/MI1.md#implementation-sequence).

## Asset extensibility plan

[AX1 — Extensible Asset Types and Polymorphic Payloads](.plan/AX1.md) owns the
generic extension mechanism. It replaces the top-level closed payload variant
with `std::shared_ptr<IAssetPayload>`, gives Asset an explicit type/loader
registry, and defines the separate offline importer-provider boundary.

AX1 preserves built-in type values, `AssetID` packing, cache transactions,
dependency ownership, observation, rollback, concurrency, and unload semantics.
It also defines the migration adapters for existing built-in loaders. Optional
modules consume the resulting registration contract; they do not design or
modify Asset's extension mechanism. AX1.1 through AX1.4 are landed; the
remaining AX1 work is verification by the Live2D consumer.

Live2D's use of the contract is documented in
[L2D2 — Live2D Asset integration](../live2d/.plan/L2D2.md).

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

- **Import is offline authoring work, not runtime loading.** The import
  library/tool may use Core, Assimp, ImageIO, serialization, and the archive
  repository, but never constructs runtime Asset identity or calls
  `AssetManager`. The native runtime loader is a separate read-only adapter.
  Texture importing and cooking follow the same rule: they may consume ImageIO
  and CPU data, but never construct runtime Asset identity or call
  `AssetManager`.
- **Packaging changes placement, not ownership.** A package table maps stable
  product identities to byte ranges. Material remains metadata with Texture
  dependencies; it does not become the owner of embedded Texture payloads.
- **Initial readiness is distinct from full residency.** Runtime decides when
  the selected scene has enough verified data to commit; Render/Graphics ensure
  only initialized mip ranges are sampleable while later ranges stream.

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
