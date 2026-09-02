# GP7.4 — Startup-Level Migration and Validation Fixtures

- Status: complete (2026-09-02)
- Parent spec: [Gameplay Level Asset (GP7)](../../../.spec/specs/gameplay-level-asset.md)
- Parent roadmap: [Gameplay TODO](../TODO.md#gp7--level-asset-and-startup-scene-migration)
- Depends on: [GP7.3](GP7.3.md)
- Affected owners: Bootstrap, Asset, Runtime, Gameplay, Render
- Future consumer: GP7.5 runtime evidence and handoff

## Outcome

Replace the bootstrap-authored scene and duplicated preload manifest with one
validated Asset-root-relative startup-level reference. Runtime loads that
`LevelResource`, commits it through the landed `LevelInstance`, possesses the
level's selected camera, and enters the frame loop only after startup commits.

GP7.4 also closes the transitive dependency hole that the bootstrap manifest
currently masks: authored materials must declare their shader-program and
texture dependencies during Asset loading, and Render must resolve those ready
dependency identities instead of interpreting paths or calling `LoadSync` on
the first frame.

Author three immutable level fixtures:

- `pbr_showcase.level` is the normal startup level and reproduces the current
  deferred-PBR scene;
- `point_shadow_validation.level` isolates the landed six-face point-shadow
  path;
- `spot_shadow_validation.level` isolates spotlight-shadow verification.

Selecting a fixture changes only `bootstrap.json`'s `startup_level` value.
Neither bootstrap C++ nor level contents are edited to switch modes. GP7.4
collects migration evidence; GP7.5 remains the final lifecycle/evidence audit
and closes GP7.

## Entry condition

GP7.1–GP7.3 are complete. Preserve the closed Level V1 schema, Asset-owned
level dependency indices, Runtime-owned transactional `LevelInstance`,
Gameplay-owned Actors/components, and value-only Render source registries.

The current live startup still follows this transitional path:

```text
bootstrap assets + scene paths
        |                |
        |                +--> Render path interpretation / source preparation
        +--> Runtime async queue --> Render-side LoadSync
                                      |
                                      +--> bootstrap sources
                                                |
Runtime FinalizeGameStartup --------------------+
        +--> static-mesh Actors
        +--> hard-coded camera/lights/controller
```

GP7.4 removes that path completely rather than layering `startup_level` beside
it. The generic async request queue may remain for future runtime streaming,
but bootstrap is no longer its producer and its comments/tests must not claim
otherwise.

## Design boundary

```text
bootstrap.json
  startup_level (Asset-root-relative path)
          |
          v
Bootstrap validation --> AssetManager::LoadSync(Level)
                                  |
                       transitive dependency graph
                       model -> mesh
                       material -> shader program + textures
                       environment -> texture
                                  |
                                  v
Runtime startup AssetID --> LevelInstance transaction --> GameplayWorld
                                                        Actors/components
                                                               |
                                                     copied source values
                                                               v
                                                      Render registries
                                                               |
                                                        Render resources
                                                               |
                                                          Graphics RHI
```

- Bootstrap owns only process startup selection and validation.
- Asset owns the startup level's CPU identity, immutable authoring data, path
  normalization, and complete dependency graph.
- Runtime owns the active level instance, startup transaction, player
  controller, and the render/game-thread startup handshake.
- GameplayWorld owns instantiated Actors and controllers.
- Render consumes ready AssetIDs and copied source values. It owns material,
  environment, pass, fallback, and GPU-resource policy.
- Graphics/RHI alone owns backend resources, execution, and synchronization.

Do not introduce `WorldResource`, level streaming, editor selection, mutable
fixture modes, backend types, or a generic startup-service framework in this
stage.

## Bootstrap V2 contract

Use one closed bootstrap schema:

```json
{
  "version": 2,
  "startup_level": "level/pbr_showcase.level"
}
```

Version 2 is intentional because removing `assets` and `scene` is a breaking
schema change. `BootstrapConfig` contains only `version` and
`startup_level`. Reject:

- a missing or unsupported version;
- missing, empty, or non-string `startup_level`;
- unknown top-level fields, including legacy `assets` or `scene`;
- absolute, drive-qualified, NUL-containing, or Asset-root-escaping paths;
- a path whose extension does not identify `KPAT_Level`.

Extract the landed level-reference normalization into one Asset-owned utility
and use it from both `LevelLoader` and bootstrap parsing/loading. The serialized
form is relative to `GetAssetDirectory()` and therefore starts with `level/`,
not `asset/level/`. Do not keep two almost-equal path validators.

Delete `BootstrapScene`, `BootstrapSceneObject`, `BuildLoadRequests`, and their
legacy parser/request tests once no consumer remains. Rename
`Engine::PreloadBootstrap` and its one-shot guard to describe configuration and
startup-level loading rather than queue preloading.

## Complete authored dependency graph

The level loader already declares direct model, material, and environment
requests, and model loading owns its mesh dependencies. Material loading is the
remaining gap: `MaterialResource` currently stores authored shader/texture
paths, while `MaterialAssetResolver` calls `AssetManager::LoadSync` when Render
first resolves the material. Removing the bootstrap preload list without
fixing this would preserve an unowned path interpretation and a render-frame
stall.

Extend material CPU metadata without changing the material JSON schema:

- the material loader validates/normalizes the shader-program reference and
  declares a typed `KPAT_ShaderProgram` dependency request;
- every authored texture parameter declares a typed `KPAT_Texture` dependency
  request in deterministic parameter order;
- `MaterialResource` retains the shader dependency index and each texture
  parameter's dependency index; paths may remain only as authoring/diagnostic
  data;
- duplicate references may share one AssetID, but each semantic retains the
  dependency index needed for deterministic resolution;
- `MaterialAssetResolver` calls `ResolveDependency` with the expected type and
  never reloads or joins paths for authored material references.

`ShaderProgramLoader` already owns its per-stage shader subresources; do not
move shader compilation or GPU objects into Asset. Add loader/resolver tests
for missing files, wrong types, repeated textures, dependency-index stability,
and proof that resolution performs no authored `LoadSync`.

Default white/flat-normal textures and Render's pass-private shaders are
engine Render resources, not authored level dependencies. Warm the two default
textures during Render startup and retain the existing explicit Render-owned
pass-shader warmup. They must not return to bootstrap, and they must not be
loaded lazily by the first material draw.

## Startup transaction and failure contract

Load the selected level synchronously on the game/main thread before spawning
the render thread. This resolves and validates all CPU Asset dependencies and
stores only the resulting `KPAT_Level` `AssetID` in `RuntimeContext`; Render
never receives the startup path or `LevelResource`.

After Render has created its window/context, initialized its resource systems,
and reported a typed ready result, Runtime commits game startup:

1. instantiate the ready level AssetID through `LevelInstance`;
2. select the level's preferred enabled camera;
3. create/activate the Gameplay input context and local controller;
4. possess that camera Actor;
5. publish startup commit so the render thread may initialize UI and enter its
   frame loop.

`FinalizeGameStartup` returns a typed result and a diagnostic. A config/load
failure occurs before render-thread creation. A level, camera, controller, or
possession failure unloads the level transaction, leaves no partial Actor or
source ownership, signals startup abort, and causes `Engine::Initialize` to
join the render thread and report failure.

Harden the current one-way render startup notification into a two-phase
handshake:

```text
render thread: Initialize/PostInitialize -> Ready(result) -> wait Commit|Abort
game thread:   wait Ready -> instantiate/possess -> Commit|Abort
render thread: Commit -> UI/frame loop
               Abort  -> ordered RuntimeContext::Clear -> exit
```

Catch render-thread startup exceptions and propagate the diagnostic through
the same result instead of allowing `std::terminate` or leaving the game thread
blocked on a condition variable. Keep this narrowly scoped to initial startup;
do not design restart, hot reload, or general task cancellation.

Lua command binding may remain in finalization, but a nonessential command
bridge failure must have an explicit policy and must not masquerade as a
partially successful level transaction. Local command transport starts only
after startup commit.

## Startup camera policy

General `LevelInstance` remains valid for camera-free levels, but a startup
level requires at least one enabled camera. Remove the implicit default camera
Actor from `FinalizeGameStartup`; camera authoring belongs in each fixture.

Render's existing active-camera rule is authoritative: highest enabled
priority wins, with lowest source-handle ID breaking ties. Runtime must possess
the same authored camera Actor. Put the comparison in one small pure helper
shared by `CameraSourceRegistry` and `LevelInstance`'s preferred-camera query;
do not duplicate the rule or add a second camera ID to bootstrap/Level V1.

Because actors are committed in authored order and camera handles are created
in that order, the first authored camera wins an equal-priority tie.
`LevelInstance` may expose the selected camera's ActorHandle after a successful
commit; it must not expose a Render source handle or mutable camera component.

## Remove transitional Render startup plumbing

After the level path is live, remove:

- `BootstrapSceneInfo` / `BootstrapSceneObjectInfo` and the corresponding
  `RenderSystemInitInfo` field;
- `SetBootstrapScene`, bootstrap scene storage, and bootstrap renderable-source
  transfer from `RuntimeContext`;
- `PrepareBootstrapRenderableSources` and `TakeBootstrapRenderableSource(s)`;
- environment classification by comparing load-request paths;
- the bootstrap environment baseline and all scene-specific Render `LoadSync`
  calls.

The environment registry becomes the only authored environment source. With no
valid level environment, Render uses its existing complete black fallback.
Render-owned resolver caches and built-ins remain private and are destroyed
before backend teardown.

Remove the hard-coded static-mesh conversion, camera, directional light, point
light, and spot light creation from `FinalizeGameStartup`. The normal scene is
fully authored in `pbr_showcase.level`.

## Fixture authoring

All fixtures use stable descriptive object IDs, explicit camera/light values,
and the same Level V1 schema. They are independent assets, not variants selected
by booleans inside one file.

### `pbr_showcase.level`

Reproduce the current bootstrap scene exactly:

- HDR environment `texture/hdr/HDR_041_Path.hdr`, IBL intensity `0.25`;
- rock root plus floor, gold sphere, quartz bunny, rusted-iron teapot, and
  Cerberus, preserving current model/material paths and transforms;
- the current perspective camera values and transform;
- the current directional light;
- point light at `(0, -20, 65)`, warm color, intensity `8000`, range `180`,
  enabled and shadowed;
- spot light at `(0, 45, 70)`, direction `(0, -0.55, -0.85)`, blue color,
  intensity `24000`, range `180`, cones `0.35/0.65`, enabled and shadowed.

Capture the old live bootstrap output before cutover when no retained baseline
exists, then compare it with the authored level on the same backend/settings.

### `point_shadow_validation.level`

Encode the deterministic D6.4 proof scene as a self-contained level:

- one enabled shadow-casting point light and no competing enabled shadow light;
- the D6.4 camera, caster/receiver placement, point position, and zero IBL
  intensity that made multi-face occlusion readable;
- only assets required by that proof.

The fixture must make `scene_color`, `point_shadow_depth`, and
`point_shadow_visibility` meaningful without editing Runtime C++ or bootstrap
scene fields. Preserve the established fixed-atlas validation expectation; do
not reopen the cubemap/subresource decision in GP7.4.

### `spot_shadow_validation.level`

Author a self-contained spotlight proof before further spotlight work:

- one enabled shadow-casting spot light and no competing enabled shadow light;
- an explicit camera, caster, receiver, and environment/intensity chosen to
  reveal cone coverage and occlusion;
- stable geometry/light placement shared by both backends.

The fixture must support `scene_color`, `spot_shadow_depth`, and
`spot_shadow_visibility`. This is fixture migration and verification support,
not a spotlight-shadow algorithm change.

## Reference gate

The local architecture was compared against these open-source precedents:

- [Godot `main/main.cpp`](https://github.com/godotengine/godot/blob/master/main/main.cpp)
  reads one `application/run/main_scene` resource identity, loads a
  `PackedScene`, instantiates it, and fails startup if the scene cannot be
  produced. Adopt the single startup-scene selection and explicit failure
  boundary; do not copy Godot's node/resource model.
- [Godot `ResourceLoader`](https://github.com/godotengine/godot/blob/master/core/io/resource_loader.cpp)
  owns path validation, caching, and completion of resource loads. This
  reinforces keeping recursive dependency loading in Asset rather than Render.
- [Piccolo's architecture documentation](https://github.com/BoomingTechDev/Piccolo/wiki)
  separates world-manager loading/ticking from resource-authored world/level
  data. Adopt the Runtime ownership direction, while keeping KimPeanutEngine's
  already-landed single-level scope and deferring a `WorldResource`.
- [Bevy's `SceneRoot` / load-completion contract](https://github.com/bevyengine/bevy/issues/15098)
  distinguishes an asset handle from readiness with dependencies. The relevant
  lesson is to commit runtime ownership only after the complete dependency
  closure is ready; KimPeanutEngine keeps its synchronous GP7.4 startup rather
  than importing Bevy's ECS/async design.

The references support selection, readiness, and ownership boundaries; they do
not justify a new scene graph, ECS, asynchronous activation, or world layer.

## Implementation stages

### GP7.4.1 — Complete Asset dependency closure

- extract the shared Asset-root-relative reference normalizer;
- make material shader/texture references typed dependency requests;
- store deterministic dependency indices in material CPU metadata;
- resolve authored material resources by dependency identity only;
- warm Render built-in textures before the first frame;
- add focused Asset/Render material contract tests.

### GP7.4.2 — Bootstrap V2 and startup Asset identity

- replace `assets`/`scene` with the closed `startup_level` schema;
- load and type-check the level before render-thread creation;
- pass one ready level AssetID into RuntimeContext;
- delete bootstrap request construction and stop enqueueing bootstrap work;
- preserve the generic async queue as an empty general-purpose seam.

### GP7.4.3 — Transactional startup and camera possession

- share the camera preference comparator;
- expose the preferred committed camera Actor from `LevelInstance`;
- make `FinalizeGameStartup` return a typed result and possess that camera;
- add the ready/commit-or-abort render startup handshake and exception
  propagation;
- prove level/controller/possession failures unload and join cleanly.

### GP7.4.4 — Remove legacy scene plumbing

- remove BootstrapScene and Render bootstrap structures/state/APIs;
- remove bootstrap renderable transfer, path comparison, and authored
  Render-side loading;
- remove hard-coded startup Actors/lights and the bootstrap environment
  baseline;
- retain only black fallback and Render-owned built-in resource warmup.

### GP7.4.5 — Author fixtures and migration evidence

- add the PBR, point-shadow, and spot-shadow Level V1 assets;
- select each fixture by changing only `startup_level`;
- run focused/full validation and dual-backend smoke/captures;
- restore `level/pbr_showcase.level` as the checked-in startup selection;
- record implementation facts and capture findings in the GP7 journal.

## Test matrix

### Bootstrap and Asset

- Bootstrap V2 accepts one normalized `.level` reference and rejects legacy,
  unknown, missing, wrong-type, absolute, and escaping inputs;
- a startup level load resolves model/mesh, material/shader/texture, and
  environment dependencies without a manual manifest;
- material dependency indices remain deterministic under repeated references;
- missing/wrong-type material dependencies fail Asset loading before Runtime
  instantiation;
- material resolution consumes ready dependency AssetIDs and performs no
  authored path join or `LoadSync`.

### Runtime startup

- valid startup commits exactly one `LevelInstance`, creates one local
  controller, and possesses the same preferred camera selected by the shared
  rule;
- camera priority, equal-priority authored-order tie-break, and disabled
  cameras are deterministic;
- camera-free startup, stale/wrong-type level ID, instantiation failure,
  controller creation failure, and possession failure abort startup and leave
  no Actor/source ownership;
- render initialization exception/failure wakes the game thread with its
  diagnostic;
- game-start failure wakes, clears, and joins the render thread without UI,
  frame-loop, or condition-variable deadlock;
- repeated initialize/clear follows the new one-shot state without duplicate
  level activation.

### Render and migration

- Render initialization has no BootstrapScene input or authored path matching;
- no environment source selects the black fallback; the authored PBR
  environment replaces it transactionally at the first frame boundary;
- Render built-in textures/pass shaders are ready before their first consumer;
- each fixture creates the expected object/source set and unload retires it;
- bootstrap and Runtime contain no duplicate PBR transforms, materials,
  environment values, camera, or light authoring.

Use injected factories/sinks and explicit startup synchronization seams. Do
not add production getters for GPU state solely to test removal.

## Validation

Follow [the validation matrix](../../validation_matrix.md). GP7.4 changes Asset
identity/dependencies, Runtime threading/startup, Gameplay composition, Render
resource resolution, public headers, CMake wiring, and live visual output, so
the expected evidence is:

1. focused Bootstrap, Asset, RuntimeLevel, Runtime startup, Gameplay, material
   resolver, environment/camera/light registry, and RenderSystem tests;
2. reconfigure as needed and build every affected test/runtime target;
3. full Debug build and complete Debug CTest suite;
4. Vulkan and OpenGL `GraphicsSmoke`;
5. rebuilt Runtime captures, selected only through `startup_level`:
   - PBR `scene_color` on both backends, compared with the pre-cutover scene;
   - point `scene_color`, `point_shadow_depth`, and
     `point_shadow_visibility` on both backends;
   - spot `scene_color`, `spot_shadow_depth`, and
     `spot_shadow_visibility` on both backends;
6. visual inspection for composition parity, camera parity, IBL, caster and
   receiver placement, atlas/cone coverage, seams/orientation, and equivalent
   backend behavior;
7. restore `startup_level` to `level/pbr_showcase.level`, then review the diff
   for legacy bootstrap fields, Render `LoadSync`/path interpretation, leaked
   backend types, accidental generated captures, and unrelated edits.

GP7.5 must still perform the final lifecycle and evidence audit. GP7.4 is not
complete based on compilation or fixture existence alone.

## Acceptance criteria

- [x] Bootstrap V2 contains only `version` and one validated
  Asset-root-relative `startup_level`.
- [x] Loading the startup level produces the complete transitive authored
  dependency graph; Render does not load or interpret authored paths.
- [x] Startup commits one Runtime-owned `LevelInstance` before the frame loop
  and possesses the same preferred enabled camera as Render selection.
- [x] Every startup failure returns a diagnostic, rolls back partial ownership,
  wakes both threads, and joins cleanly.
- [x] All BootstrapScene, bootstrap renderable-source transfer, duplicated
  preload-manifest, hard-coded startup Actor/light, and bootstrap environment
  plumbing is removed.
- [x] `pbr_showcase.level` reproduces the current normal scene and is the
  checked-in startup selection after validation.
- [x] Point- and spot-shadow fixtures are self-contained and selectable without
  changing C++ or level contents.
- [x] Focused tests, full Debug build/CTest, Vulkan/OpenGL smoke, and the
  required rebuilt-runtime captures pass and are recorded in the GP7 journal.
- [x] No Level/Bootstrap/common contract exposes Gameplay pointers, Render
  proxies, GPU handles, or backend-native types.

## Risks deferred beyond GP7.4

- asynchronous level loading, progress reporting, cancellation, transitions,
  and streaming;
- `WorldResource` or multiple active LevelInstances;
- editor level selection/save-back and cooked package manifests;
- runtime material hot reload and dependency graph mutation;
- generic engine restart/recovery after a committed frame loop;
- point-shadow cubemap/subresource RHI work without new measured evidence.
