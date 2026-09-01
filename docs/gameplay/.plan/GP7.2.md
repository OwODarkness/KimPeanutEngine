# GP7.2 — Static-Mesh Level Instantiation and Rollback

- Status: complete (2026-09-01)
- Parent spec: [Gameplay Level Asset (GP7)](../../../.spec/specs/gameplay-level-asset.md)
- Parent roadmap: [Gameplay TODO](../TODO.md#gp7--level-asset-and-startup-scene-migration)
- Depends on: [GP7.1](GP7.1.md)
- Affected owners: Runtime, Gameplay
- Read-only input owner: Asset
- Future consumers: GP7.3 level-object expansion, GP7.4 startup-level migration

## Outcome

Add one Runtime-owned `LevelInstance` that consumes an already loaded
`LevelResource`, preflights every static-mesh record, and then creates the
corresponding Actors through `CreateStaticMeshActor`. The instance retains the
stable authored-ID-to-`ActorHandle` mapping and can destroy exactly the Actors
it created.

Instantiation is transactional: a validation or creation failure leaves the
instance empty, no level-created Actor remains in `GameplayWorld`, and every
published render source from the failed attempt is retired. A caller can retry
immediately without first ticking the world.

GP7.2 does not select or load a startup-level path, replace the bootstrap
scene, instantiate lights/cameras/environment, evict Assets, or create Render
or GPU objects.

## Entry condition

GP7.1 is complete. Its focused Asset suite, full Debug build, and complete
CTest suite must remain green. The V1 schema and dependency indices are input
contracts for this stage and must not be reinterpreted in Runtime.

The existing bootstrap scene remains the live startup path until GP7.4.
GP7.2 may add Runtime ownership and a headless service seam, but it must not
silently run a level alongside the bootstrap objects.

## Design boundary

```text
AssetManager cache                 Runtime
  LevelResource AssetID ----------> LevelInstance
  dependency edges                       |
  ModelResource -> Mesh AssetID           | preflight value descriptions
  Material AssetID                        v
                                    GameplayWorld
                                         |
                              CreateStaticMeshActor
                                         |
                              Actor + MeshComponent
                                         |
                               copied Render source
```

- Asset owns immutable level/model/material/mesh data, identity, cache
  residency, and dependency edges.
- Runtime owns the active level-instance state and authored-ID map.
- `GameplayWorld` owns Actor memory, handles, initialization, activation, tick
  order, and destruction.
- Existing Gameplay factories own concrete Actor/component composition.
- Render receives copied source descriptions and remains unaware of levels.
- Graphics/RHI receives no new contract in this stage.

Do not move the authored-ID map into `GameplayWorld`, add level knowledge to
Render, or let `LevelInstance` construct components directly.

## Public contract

Create a focused Runtime level module under `engine/runtime/level/`, rather
than adding level-instance policy to `runtime_global_context.cpp`. Its primary
type is a non-copyable `kpengine::runtime::LevelInstance` with:

- non-owning references to `asset::AssetManager` and `gameplay::GameplayWorld`;
- an `Empty` or `Active` state;
- the active level `AssetID` while active;
- an authored-ID-to-`ActorHandle` map;
- creation order retained separately for reverse-order rollback and unload;
- `Instantiate(level_asset_id)`, valid only while empty;
- idempotent `Unload()`;
- `IsActive()`, `GetLevelAsset()`, `GetActorCount()`, and
  `FindActor(authored_id)` queries.

Return a typed result from `Instantiate`, not a bare boolean. At minimum,
distinguish invalid state, invalid/wrong-type level asset, dependency failure,
missing model mesh, invalid mesh data, and Actor creation failure. Include a
concise diagnostic with the authored object ID and dependency role where
applicable. Diagnostics are evidence; callers must branch on the error value,
not parse its text.

`FindActor` must revalidate the stored generational handle through
`GameplayWorld::FindActor`. It returns no Actor handle when the authored ID is
unknown or the stored handle is stale; the map never becomes a second owner.

Use a narrow injectable static-mesh creation callable whose production default
invokes `CreateStaticMeshActor`. This is a failure-injection seam for proving
mid-commit rollback, not a general reflected Actor factory or service locator.

## Static-mesh mapping

Process `LevelResource::objects` in authored file order. For each
`LevelStaticMeshRecord`:

1. Resolve `model.dependency_index` through
   `AssetManager::ResolveDependency(level_id, index, KPAT_Model)`.
2. Fetch the live `Asset` through the instance's AssetManager reference,
   validate its type, and extract its `ModelResource` payload.
3. Read `ModelGeometryType::KPMG_Mesh` with `ModelResource::GetData`; do not use
   `ModelResource::GetMesh`, which hides a global AssetManager lookup.
4. Fetch and type-check the resulting `KPAT_Mesh` Asset and `MeshResource`
   payload through that manager reference, and require valid local bounds.
5. Resolve `material.dependency_index` as `KPAT_Material` and validate that the
   live cache entry and `MaterialResource` payload have the expected type.
6. Convert `LevelTransform` directly to `Transform3f`: position, Euler
   `rotation_degrees`, and scale retain their authored values and units.
7. Copy mesh/material AssetIDs, local bounds, `visible`, `casts_shadow`, and
   integer `lod_bias` into `StaticMeshActorDesc`.

Do not infer dependencies from stored paths, reload files, recalculate bounds,
or retain pointers into `LevelResource`. GP7.1's dependency indices are the
authoritative link from the owning level to its loaded Assets.

Known V1 directional-light, point-light, spot-light, camera, and environment
records are deliberately ignored by GP7.2. A level containing no static meshes
successfully becomes an active zero-Actor instance; GP7.3 will add the other
record consumers without changing this stage's static-mesh mapping.

## Transaction and state transitions

### Phase A — preflight without world mutation

Resolve and validate the level plus every static-mesh dependency, then build a
temporary ordered array of `(authored_id, StaticMeshActorDesc)`. Do not create
an Actor or publish a source during this phase. Any failure discards the
temporary array and leaves the instance `Empty`.

Although GP7.1 rejects duplicate IDs at parse time, guard insertion into the
runtime map as an invariant check. Never silently overwrite an existing
mapping.

### Phase B — ordered commit

Create Actors in authored order with the existing Gameplay factory. After each
successful creation, append the authored ID and handle to the temporary commit
set. Publish the active level ID and map only after every creation succeeds.

If creation fails, destroy all Actors in the temporary commit set in reverse
order, reclaim their destroyed world entries immediately, discard all
temporary state, and return `ActorCreationFailed`. This provides the strong
transaction guarantee: failure is observationally equivalent to no active
level instance, apart from balanced create/destroy source events.

Calling `Instantiate` while active returns `InvalidState` and does not unload,
replace, or partially modify the current instance. Level replacement policy is
deferred until a real transition/streaming consumer exists.

### Deterministic unload

`Unload()` destroys mapped Actors in reverse creation order, asks
`GameplayWorld` to reclaim destroyed entries, and only then clears the map,
creation order, active level ID, and state. It is safe to call on an empty
instance and from the destructor while the referenced world and sinks remain
alive.

Actor destruction retires component-owned Render source tokens through the
existing sink contract. `LevelInstance` never holds or destroys those tokens
itself.

## GameplayWorld reclamation seam

Today `DestroyActor` invalidates the handle and calls `Actor::Destroy`, but
map-entry reclamation normally waits for the end of `GameplayWorld::Tick`.
That is unsafe for a transaction that must support immediate retry: the handle
slot can be reused while the destroyed Actor still occupies the same map key.

Expose the existing reclamation operation as a narrowly documented
game-thread lifecycle boundary, for example
`GameplayWorld::ReclaimDestroyedActors()`. It must not be invoked from inside
world iteration. `LevelInstance` calls it only after reverse-order rollback or
unload. Do not change `DestroyActor` globally to erase immediately and do not
advance the world with a synthetic zero-time tick.

Focused Gameplay tests must lock down the reclamation rule independently of
the level-instance tests.

## Runtime integration and shutdown order

Add a `RuntimeLevel` library for the focused module and a corresponding unit
test target. `RuntimeLevel` depends on Asset and Gameplay; it must not depend on
Editor, Render internals, or a graphics backend.

`RuntimeContext` owns one `LevelInstance`, constructs it after
`GameplayWorld`, and unloads/resets it before `GameplayWorld` and
`RenderSystem` teardown. GP7.2 does not call `Instantiate` from
`FinalizeGameStartup`; GP7.4 will supply `startup_level` and remove bootstrap
scene creation after fixture parity is proven.

Required lifetime order is:

```text
LevelInstance::Unload
  -> Gameplay Actors/components destroyed
  -> copied Render sources retired
  -> GameplayWorld destroyed
  -> RenderSystem destroyed
```

## Asset residency policy

An active GP7.2 instance retains AssetIDs, not ownership pointers or an
implicit cache lease. The current Runtime does not evict referenced Assets
during play, and `AssetManager` owns all registration/unregistration policy.

Therefore:

- `LevelInstance::Unload` destroys runtime Actors but does not call
  `UnRegisterAsset` for the level or its dependencies;
- Runtime must not explicitly unregister the active level or dependencies;
- Asset dependency edges remain governed by the GP7.1 graph;
- a generic external pin/lease API is outside GP7.2 because no live eviction or
  streaming consumer currently needs it.

Record the absence of external Asset pins as a known limitation. Before runtime
eviction, hot reload, multiple simultaneous level instances, or streaming is
introduced, Asset must gain an explicit residency contract and tests. Do not
approximate that future contract by storing `shared_ptr` payloads in Gameplay.

## Implementation stages

### GP7.2.1 — Gameplay reclamation contract

- expose and document the existing destroyed-Actor reclamation boundary;
- verify destruction invalidates handles, retires sources, and permits
  immediate handle-slot reuse only after reclamation;
- keep normal end-of-tick reclamation behavior unchanged.

### GP7.2.2 — Runtime level-instance core

- add the RuntimeLevel target, typed state/result contract, and queries;
- implement Asset-only preflight and exact model-to-mesh mapping;
- implement ordered commit through the static-mesh factory;
- skip known GP7.3 record kinds without treating them as errors.

### GP7.2.3 — Rollback and unload

- add injected creation failure coverage;
- roll back partial commits in reverse order and reclaim immediately;
- implement idempotent reverse-order unload and destructor cleanup;
- prove an immediate retry succeeds after rollback.

### GP7.2.4 — Runtime ownership and handoff

- make `RuntimeContext` own the dormant instance and enforce shutdown order;
- leave bootstrap startup behavior unchanged;
- run focused and full validation;
- record implementation evidence in the existing GP7 journal before checking
  off GP7.2.

## Test matrix

Add headless tests with an isolated AssetManager setup, GameplayWorld, and fake
Render source sinks. Cover at least:

- a valid level creates static meshes in file order with exact transforms,
  mesh/material IDs, bounds, flags, and integer LOD bias;
- authored-ID lookup succeeds and stale/unknown lookup fails;
- light, camera, and environment records are skipped, including a valid
  zero-static-mesh level;
- a stale/wrong-type level ID fails before world mutation;
- stale, wrong-type, or mismatched model/material dependency edges fail before
  world mutation;
- a model without `KPMG_Mesh`, a stale/wrong-type mesh ID, and invalid bounds
  fail before world mutation;
- an injected failure after at least one successful Actor creation produces
  balanced source creates/destroys, no remaining level Actor, an empty map, and
  `Empty` state;
- immediate retry after rollback succeeds without a world tick;
- a second instantiate while active preserves the first instance;
- unload uses reverse creation order, invalidates handles, retires sources,
  reclaims destroyed entries, and is idempotent;
- destructor cleanup matches explicit unload;
- unload does not unregister the level or dependency Assets.

Avoid assertions against unordered-container iteration order. Capture explicit
factory and sink event order instead.

## Validation

Follow [the validation matrix](../../validation_matrix.md). Because this stage
changes Runtime and Gameplay lifecycle behavior, expected evidence is:

1. focused `RuntimeLevelTest`, `GameplayUnitTest`, and relevant `AssetUnitTest`
   cases;
2. targeted Debug builds for RuntimeLevel, Gameplay, Asset, and RuntimeLib;
3. full Debug build;
4. complete Debug CTest suite;
5. diff review confirming no bootstrap scene, Render contract, RHI, backend,
   or serialized V1 schema change.

No Vulkan/OpenGL capture is required for GP7.2 because it deliberately does
not enter the live startup path. Rendered fixture parity belongs to GP7.4 and
dual-backend visual evidence belongs to GP7.5. Headless source-lifecycle
evidence is required here; compilation alone is insufficient.

## Acceptance criteria

- [x] Runtime owns one non-copyable level instance; GameplayWorld remains the
  sole Actor owner.
- [x] Every static-mesh record is fully preflighted before the first Actor is
  created.
- [x] Model dependencies resolve deterministically through
  `ModelResource::GetData(KPMG_Mesh)` and explicit live-Asset validation.
- [x] Successful creation preserves authored order and stable authored-ID
  lookup without storing Actor pointers.
- [x] Any failure leaves the instance empty and no level-created Actor or live
  source remains.
- [x] Rollback supports immediate retry without a GameplayWorld tick.
- [x] Unload is reverse-order, idempotent, and completes before Gameplay/Render
  teardown.
- [x] Known GP7.3 records do not block a GP7.2 static-mesh instance.
- [x] LevelInstance performs no Asset loading, unregistration, Render-policy,
  GPU, or backend work.
- [x] Bootstrap scene behavior and the LevelResource V1 format are unchanged.
- [x] Focused tests, full Debug build, and complete CTest pass, with factual
  results recorded in the GP7 journal.

## Reference gate

- [Piccolo `Level::load`, `createObject`, and `unload`](https://github.com/BoomingTech/Piccolo/blob/main/engine/source/runtime/function/framework/level/level.cpp)
  separate a CPU level resource from a stable map of runtime objects and an
  explicit unload boundary. GP7.2 adopts that resource/instance split and
  exact instance membership, while rejecting global-manager lookup and
  non-transactional partial creation.
- [Godot `SceneState::instantiate`](https://github.com/godotengine/godot/blob/master/scene/resources/packed_scene.cpp)
  separates serialized scene state from its runtime node tree and cleans up
  failed construction. GP7.2 adopts separate preflight/commit state and
  mandatory cleanup, while rejecting reflection, arbitrary class/property
  construction, scene inheritance, and editor placeholders.
- [Bevy 0.15-to-0.16 scene migration guidance](https://bevy.org/learn/migration-guides/0-15-to-0-16/)
  documents instance-based despawn as removing all entities associated with a
  scene instance even when hierarchy changes. GP7.2 likewise owns an explicit
  membership map and unloads from that map rather than walking scene
  hierarchy. KimPeanut retains GameplayWorld as the actual Actor owner.

These references validate ownership, transaction, and membership patterns;
they are not source templates.

## Risks and deferred work

- `GameplayWorld` reclamation is currently tick-coupled. The narrow explicit
  reclamation seam must be documented and tested so transaction rollback does
  not become a general mid-tick mutation tool.
- The Actor-creation callable exists solely to prove rollback. If future object
  kinds accumulate unrelated callable plumbing, GP7.3 should consolidate only
  the concrete shared behavior rather than introducing a reflected factory.
- AssetIDs do not pin cache residency. This is acceptable for the current
  no-eviction Runtime, but blocks safe streaming/hot reload until Asset defines
  an explicit lease or equivalent ownership contract.
- GP7.2 cannot visually prove scene parity because bootstrap intentionally
  remains authoritative. Do not weaken GP7.4/GP7.5 runtime and dual-backend
  evidence on the strength of GP7.2's headless tests.
- A second concurrent level instance, transitions, parent/child hierarchy, and
  `WorldResource` remain deferred until they have a concrete consumer and
  lifetime policy.
