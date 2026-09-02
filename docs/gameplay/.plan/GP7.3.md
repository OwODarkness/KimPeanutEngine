# GP7.3 — Level Lights, Cameras, and Environment Source

- Status: complete (2026-09-01)
- Parent spec: [Gameplay Level Asset (GP7)](../../../.spec/specs/gameplay-level-asset.md)
- Parent roadmap: [Gameplay TODO](../TODO.md#gp7--level-asset-and-startup-scene-migration)
- Depends on: [GP7.2](GP7.2.md)
- Affected owners: Runtime, Gameplay, Render
- Read-only input owner: Asset
- Future consumers: GP7.4 startup-level migration, GP7.5 runtime evidence

## Outcome

Extend the landed Runtime-owned `LevelInstance` so the complete V1 authored
object set is instantiated transactionally:

- directional, point, and spot records create the existing Gameplay light
  Actor compositions;
- camera records create the existing Gameplay camera composition and continue
  to use Render's deterministic camera-source selection;
- the optional level-wide environment publishes one copied, value-only source
  containing a ready texture `AssetID` and IBL intensity;
- Render resolves that source to its existing panorama and IBL bindings at a
  frame boundary without learning about `LevelResource` or authored paths.

All Actor kinds share GP7.2's authored-ID map, reverse-order rollback, immediate
world reclamation, and deterministic unload. The environment source is
registered last and retired first, so a registration failure can still roll
back every Actor and leave the instance empty.

GP7.3 does not load a startup level, possess a level camera, delete bootstrap
scene fields, create fixture files, change the V1 schema, or introduce
multi-environment blending. Those remain GP7.4 or later work.

## Entry condition

GP7.1 and GP7.2 are complete. Preserve the landed Asset schema, dependency
indices, static-mesh mapping, Runtime ownership, and immediate rollback/retry
guarantees. The focused RuntimeLevel and Gameplay tests, full Debug build, and
complete CTest suite must remain green.

The bootstrap camera, lights, and environment remain the live startup behavior
until GP7.4. `LevelInstance` stays dormant in `RuntimeContext` during GP7.3;
tests exercise the new source path without publishing a duplicate production
scene.

## Design boundary

```text
Asset LevelResource
  object variants ------------------------+
  environment -> Texture AssetID          |
                                            v
Runtime LevelInstance                 GameplayWorld
  transaction + authored-ID map  ---> typed Actor factories
          |                                 |
          |                                 +--> light source handles
          |                                 +--> camera source handles
          |
          +--> EnvironmentSourceDesc + opaque handle
                              |
                              v
                    Render source registries
                              |
                   frame-boundary resolution
                              |
                  Render-owned IBL bindings
                              |
                         Graphics RHI
```

- Asset owns immutable level/environment data, identities, and dependency
  edges.
- Runtime owns level membership, the environment registration handle, and the
  transaction boundary.
- GameplayWorld remains the sole Actor owner. Existing components own their
  light/camera source handles.
- Render owns source command queues, active camera/environment selection,
  resource processing, GPU bindings, fallbacks, and retirement timing.
- Graphics/RHI alone owns GPU resources and backend translation.

Do not add `LevelResource`, file paths, `TextureResource` pointers, Gameplay
components, render proxies, or backend handles to a cross-module source
description.

## Closed typed Actor planning

Replace GP7.2's static-mesh-only `PendingActor` with an explicit closed variant
of the existing Gameplay factory descriptions:

- `StaticMeshActorDesc`;
- `DirectionalLightActorDesc`;
- `PointLightActorDesc`;
- `SpotLightActorDesc`;
- `CameraActorDesc`.

Use one `LevelActorFactorySet` containing five typed creation callables. Each
production default calls the corresponding concrete Gameplay factory. The
bundle is a deterministic failure-injection seam for tests; it is not a
reflected class registry, string dispatch table, service locator, or promise of
arbitrary component construction.

Preflight converts every `LevelObject` in authored order into a temporary
`(authored_id, typed_description)` entry before creating the first Actor.
Validate empty/duplicate IDs across all object kinds, even though GP7.1 already
rejects them at load time. Commit visits the closed variant and calls exactly
one typed factory. Publish the active map only after all Actors and the optional
environment registration succeed.

`GetActorCount` and `FindActor` now cover every authored object kind. The
level-wide environment has no authored object ID and is not counted as an
Actor.

## Camera composition naming and selection

The current `CreateFreeCameraActor` composition creates only an Actor plus a
`CameraComponent`; free movement actually comes from later
`PlayerController::Possess`. Rename the neutral composition to
`CameraActorDesc` and `CreateCameraActor`, updating current Runtime/tests rather
than teaching level instantiation that every authored camera is a free camera.
Do not add a permanent duplicate factory API.

Map camera values directly:

- `LevelTransform` to `Transform3f` with authored degree units unchanged;
- perspective/orthographic to the existing `CameraProjectionMode` enum;
- field of view, near/far planes, orthographic height, enabled, and priority
  without introducing Runtime defaults.

The existing `CameraSourceRegistry` remains authoritative for the active
camera: highest enabled priority wins, and equal priority resolves to the
lowest source-handle ID. Because Actors are created in file order, the first
authored camera wins a tie deterministically. GP7.3 does not duplicate this
selection in `LevelInstance` and does not choose which camera a controller
possesses; GP7.4 must make controller/bootstrap policy explicit.

When no camera source remains after unload, Render continues to apply its
existing default-camera fallback. GP7.4 decides when that transitional fallback
is no longer needed.

## Light mappings

Map V1 records one-to-one into the existing factory descriptions:

| Level record | Gameplay factory | Copied values |
|---|---|---|
| `LevelDirectionalLightRecord` | `CreateDirectionalLightActor` | direction, color, intensity, enabled, casts-shadow intent |
| `LevelPointLightRecord` | `CreatePointLightActor` | position, color, intensity, range, enabled, casts-shadow intent |
| `LevelSpotLightRecord` | `CreateSpotLightActor` | position, direction, color, intensity, range, inner/outer cones, enabled, casts-shadow intent |

Preserve authored directions in the source values. Normalization needed for
shadow fitting or shading remains Render policy and already occurs in the
current consumers. Do not serialize or manufacture `LightHandle`,
`ShadowHandle`, binding slots, shadow-map resolution, bias, or atlas layout.

The light components continue to own opaque `LightSourceHandle`s and retire
them on deactivation. `LevelInstance` owns only Actor handles for lights.

## Environment source contract

Add a small Render-owned `environment_source.h` contract:

- `EnvironmentSourceDesc` contains a valid `KPAT_Texture` AssetID and finite,
  non-negative `ibl_intensity`;
- `EnvironmentSourceHandle` is a private generational registration token;
- `IEnvironmentSourceSink` exposes queued create and destroy operations.

Do not add update in GP7.3: `LevelResource` is immutable and has no current
runtime environment-edit consumer. A later measured editor or transition use
case can add update semantics without pretending they exist today.

An `EnvironmentSourceRegistry` owns the game-thread-safe command inbox and
copied active descriptor. It permits at most one live registration. A second
create while the first handle is live fails deterministically; destroying the
first handle permits a new create immediately, even before Render drains the
ordered destroy/create commands. Stale or duplicate destroys fail without
changing the selected source.

This single-source rule matches V1's one level-wide environment and GP7.2's one
active `LevelInstance`. Do not add priorities, spatial volumes, blending, or
multiple-level arbitration before a transition/streaming design exists.

`RenderSystem::GetEnvironmentSourceSink()` exposes only this contract. Runtime
passes the borrowed sink into its `LevelInstance`; GameplayWorld and Actors do
not gain environment knowledge. Because `LevelInstance`'s public contract now
names the Render-owned sink, RuntimeLevel must declare a direct dependency on
Render rather than relying on Gameplay's transitive link; it still has no
Graphics, backend, or Editor dependency.

## Environment preflight and registration

If the level has an environment, preflight must:

1. resolve its dependency index from the level as `KPAT_Texture`;
2. validate the live Asset wrapper and non-null `TextureResource`/data payload;
3. copy only the texture AssetID and authored IBL intensity into
   `EnvironmentSourceDesc`;
4. fail before Actor creation when the dependency is stale, wrong-typed, or
   has no CPU texture payload.

Do not reload the authored path or retain `TextureResource` data in Runtime.
The Resource/Render consumer remains responsible for the stronger panorama
format requirement. A non-RGBA16F or malformed pixel payload may therefore
register successfully but must fail Render resolution deterministically and
fall back safely; that is a render-resource failure, not a partial
level-instantiation failure.

After all Actors have been created, register the optional environment. If the
sink is absent, already occupied, rejects the descriptor, or returns an invalid
handle, destroy the created Actors in reverse order, reclaim immediately, and
return a typed `EnvironmentSourceCreationFailed` result. On success, retain the
opaque handle privately and publish the active level state.

No environment record means no environment source command. It remains valid
for a level to contain no camera, no lights, or no environment.

## Render resolution and fallback

Drain environment commands in `RenderSystem::BeginFrame` after Runtime load
requests are consumed and before lighting/pass recording. The registry exposes
a copied optional descriptor; Render resolves its ready AssetID through
AssetManager without calling `LoadSync` or comparing paths.

Refactor the current loose environment fields into a small private value bundle
that groups:

- source AssetID;
- visible panorama binding;
- irradiance binding;
- prefiltered-radiance binding;
- BRDF-LUT binding;
- prefilter level count;
- IBL intensity and enabled state.

Resolution is transactional. Build the panorama and derived IBL bindings into
a temporary bundle and replace the selected bundle only when all required
bindings are valid. Partial resolver-cache entries remain Render-owned and are
released by the existing resolver cleanup; they must never become a mixed
active environment.

Keep three concepts distinct during staged migration:

1. always-valid black fallback bindings required by deferred descriptors;
2. the existing bootstrap environment, retained as the legacy baseline through
   GP7.3;
3. an optional level environment override selected by the new source registry.

With no valid level source, use the bootstrap baseline when present, otherwise
the black fallback. Destroying a source returns to that baseline at the next
drain. A failed new resolution logs one path-independent diagnostic and also
uses the baseline; it does not leave half-new bindings or stale level intensity
active.

An intensity-only difference for the same resolved texture changes the scalar
without repeating panorama upload or CPU IBL convolution. Destroying and
re-registering the last successfully resolved texture may reuse its retained
Render-owned bundle. Do not expose that cache to Runtime or Gameplay.

The visible panorama and material IBL continue to use the same source AssetID,
while `ibl_intensity` scales material IBL only. Preserve the current visible-sky
behavior.

## Transaction and unload order

The complete state transition is:

```text
Empty
  -> preflight every Actor + optional environment
  -> create Actors in authored order
  -> register environment last
  -> publish map, creation order, environment handle, active level
Active
```

Any preflight failure mutates neither Gameplay nor Render source queues. Any
Actor factory failure rolls back already created Actors. Environment
registration failure also rolls back every Actor. In every case the instance
returns to `Empty` and supports immediate retry.

Unload reverses commit order:

1. enqueue environment destroy, if present;
2. destroy all Actors in reverse authored creation order;
3. reclaim destroyed GameplayWorld entries;
4. clear the environment handle, Actor map/order, level AssetID, and active
   state.

At the next Render drain, environment, camera, and light registries retire the
copied sources before pass recording. Runtime shutdown retains the landed
ordering: `LevelInstance`, then GameplayWorld, then RenderSystem. RenderSystem
shutdown clears all pending source registries before resource-resolver/backend
cleanup.

## Error contract

Preserve GP7.2 errors and add only errors with distinct caller meaning:

- `InvalidEnvironmentResource` for a resolved texture dependency with no live
  usable CPU payload;
- `EnvironmentSourceCreationFailed` for missing/occupied/rejecting environment
  sink registration.

Light and camera factory failures use `ActorCreationFailed`, with the authored
kind and ID in the diagnostic. Do not add one enum value per Actor kind when
the recovery action is identical. Render-side panorama processing or GPU
binding failure is reported by Render diagnostics because it occurs after the
queued source transaction has crossed the module boundary.

## Implementation stages

### GP7.3.1 — Neutral camera and typed factory set

- rename the camera-only composition from free-camera to camera;
- introduce the explicit five-factory bundle and typed pending variant;
- map all light/camera values and extend authored-ID coverage;
- preserve file-order creation and GP7.2 static-mesh behavior.

### GP7.3.2 — Environment source lifecycle

- add the value-only descriptor, opaque handle, sink, and single-source
  registry;
- expose the sink from RenderSystem and pass it to the dormant LevelInstance;
- test validation, ordered destroy/create, stale handles, and shutdown clear.

### GP7.3.3 — Transactional environment resolution

- drain the registry at the frame boundary without loading or path matching;
- group environment Render state and resolve a temporary complete binding
  bundle;
- distinguish fallback, bootstrap baseline, and level override;
- prove intensity-only reuse and failure fallback.

### GP7.3.4 — Full LevelInstance transaction and handoff

- preflight the optional environment dependency;
- register environment after all Actors and roll back on failure;
- retire environment before Actors on unload;
- run focused/full validation and record factual evidence in the GP7 journal;
- leave `FinalizeGameStartup` and bootstrap authoring unchanged.

## Test matrix

### RuntimeLevel

- one mixed level creates static mesh, directional, point, spot, and camera
  Actors in exact authored order and maps every authored ID;
- each factory receives exactly the V1 values, including shadow intent, camera
  projection, priority, and transform degree units;
- equal-priority cameras are published in authored order for deterministic
  Render tie-breaking;
- missing object kinds and a level containing only environment remain valid;
- stale/wrong-type environment dependencies and missing texture payloads fail
  before any Actor/source creation;
- failure injected into each non-static factory rolls back prior mixed Actors
  and permits immediate retry;
- missing/occupied/rejecting environment sink after Actor creation rolls back
  all Actors and leaves no live environment handle;
- unload destroys environment first, then Actors in reverse order, and remains
  idempotent;
- `GetActorCount` and stale/unknown authored-ID lookup cover all Actor kinds;
- GP7.2 static-mesh-only and no-environment behavior remains unchanged.

### Gameplay and source registries

- every light factory publishes the correct copied source and retires it once;
- camera factory naming change preserves component/source/controller behavior;
- camera priority, equal-priority handle tie-break, disabled-camera fallback,
  and stale source destruction remain deterministic;
- environment registry accepts one valid source, rejects invalid descriptors
  and concurrent ownership, allows ordered destroy/create replacement, rejects
  stale handles, and clears pending/active state safely.

### RenderSystem

- no environment source preserves the current bootstrap output state;
- a valid ready texture AssetID resolves panorama plus all three IBL artifacts
  without `LoadSync` or path comparison;
- all bindings switch as one bundle before pass recording;
- intensity-only change/re-registration of the same texture avoids repeated IBL
  processing;
- source destroy restores bootstrap baseline or black fallback;
- wrong format, malformed data, processing failure, or backend binding failure
  leaves a complete fallback/baseline and one actionable diagnostic;
- shutdown drains/clears environment commands before resolver and backend
  destruction.

Use recording factories/sinks and the existing injected Render backend. Do not
add public production getters solely to inspect private GPU handles in tests.

## Validation

Follow [the validation matrix](../../validation_matrix.md). Because GP7.3
changes Gameplay composition, Render source contracts, and frame-boundary GPU
resolution, expected evidence is:

1. focused `RuntimeLevelTest`, `GameplayUnitTest`, environment-source registry,
   camera/light registry, and `RenderSystemTest` cases;
2. targeted Debug builds for RuntimeLevel, Gameplay, Render, and RuntimeLib;
3. shader syntax/contract checks affected by any environment binding-state
   cleanup, even though binding slots must not change;
4. full Debug build and complete Debug CTest suite;
5. Vulkan and OpenGL `GraphicsSmoke`, because the Render public source contract
   and environment binding lifecycle change;
6. one rebuilt-runtime SceneColor capture on each backend, compared with the
   current bootstrap environment baseline to prove the state refactor does not
   regress visible sky or IBL;
7. diff review confirming no V1 schema, bootstrap startup selection, RHI, or
   backend-native contract change.

GP7.3 does not enter the new level source into the live startup path, so these
captures prove preservation of the legacy baseline rather than authored-level
parity. GP7.4 owns the first level-driven visual comparison and GP7.5 owns final
dual-backend evidence. Headless lifecycle plus Render binding-failure tests are
also required; compilation or baseline screenshots alone are insufficient.

## Acceptance criteria

- [x] Every V1 object kind maps to an existing, neutral Gameplay Actor
  composition through a closed typed factory set.
- [x] All object kinds participate in one preflight, authored-order commit,
  stable-ID map, reverse rollback, and deterministic unload.
- [x] Camera activity remains Render-selected by enabled/priority/handle order;
  LevelInstance does not duplicate selection or controller policy.
- [x] Light records publish copied authoring values only; shadow resources and
  scheduling remain Render-private.
- [x] The environment source contains only texture AssetID plus IBL intensity
  and supports one explicit generational owner.
- [x] Environment source registration is the final transaction step and its
  failure leaves no partial Actors or source handle.
- [x] Render consumes a ready AssetID without `LoadSync`, authored path
  comparison, Gameplay/Level knowledge, or backend types in common contracts.
- [x] Environment binding replacement is all-or-nothing and an absent/failed
  level source restores a complete bootstrap or black fallback.
- [x] Unload retires environment, camera, light, and renderable sources before
  Gameplay/Render teardown and supports immediate retry.
- [x] Bootstrap live behavior and the LevelResource V1 schema are unchanged.
- [x] Focused tests, full Debug build, complete CTest, dual-backend smoke, and
  baseline visual captures pass, with factual evidence recorded in the GP7
  journal.

## Reference gate

- [Piccolo `Level::load`, `createObject`, and `clear`](https://github.com/BoomingTech/Piccolo/blob/main/engine/source/runtime/function/framework/level/level.cpp)
  instantiate heterogeneous authored object records into one level-owned
  membership set and clear that set on unload. GP7.3 keeps one closed typed
  transaction and membership map, while rejecting Piccolo's global managers,
  generic reflected object loading, and partial-success load behavior.
- [Godot `WorldEnvironment`](https://github.com/godotengine/godot/blob/master/scene/3d/world_environment.cpp)
  registers environment state when entering a world, clears/reselects it when
  leaving, and warns that only the first environment affects an instantiated
  scene set. GP7.3 adopts explicit world-level registration and singular
  selection, but uses an opaque queued handle rather than scene-tree groups or
  reflected Resources.
- [Godot `Camera3D`](https://github.com/godotengine/godot/blob/master/scene/3d/camera_3d.cpp)
  separates camera-node lifetime from viewport current-camera selection and
  reselects on removal. GP7.3 likewise keeps camera Actor creation separate
  from Render's active-source policy; KimPeanut retains its existing priority
  and generational-handle tie-break instead of importing viewport semantics.
- [Filament `Scene`](https://github.com/google/filament/blob/main/filament/include/filament/Scene.h)
  exposes a single replaceable scene skybox and `IndirectLight`, distinct from
  ordinary light entities. GP7.3 adopts a singular scene-level environment and
  separate direct-light path, while Render—not Runtime—continues to own the
  derived IBL/GPU resources.

These references validate the ownership and selection decisions; they are not
source templates.

## Risks and deferred work

- The first new environment AssetID still performs CPU IBL convolution and GPU
  binding creation synchronously on the Render frame boundary. This is bounded
  to startup in GP7, but it must be measured during GP7.5 and replaced with a
  staged/asynchronous preparation contract before runtime environment switching
  or streaming is enabled.
- RenderSystem already owns too much pass-private environment state. GP7.3 may
  group and make that state transactional, but must not build a parallel
  renderer facade or pre-empt the R1.2 implementation extraction.
- Component activation currently permits headless GameplayWorld use when a
  light/camera sink is absent. GP7.3 must not globally redefine that useful
  test behavior merely to make level tests stricter; production tests should
  supply recording sinks and prove balanced publication.
- The single environment registration deliberately prevents overlapping level
  environments. Cross-fade, priority, spatial probes, and concurrent level
  instances require a separate world/transition design.
- Camera priority chooses what Render sees, while controller possession remains
  independent. GP7.4 must explicitly bind player control to the intended
  authored camera instead of assuming those policies are identical.
- AssetIDs still do not pin cache residency. The GP7.2 limitation remains: no
  runtime eviction, hot reload, or streaming is safe until Asset gains an
  explicit residency contract.
