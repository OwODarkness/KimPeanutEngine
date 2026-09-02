# Gameplay Level Asset (GP7)

- Status: GP7 complete (2026-09-02)
- Owner: user + Codex
- Parent TODO: [Gameplay Module TODO](../../docs/gameplay/TODO.md#gp7--level-asset-and-startup-scene-migration)
- Related design: [Gameplay Module Design](../../docs/gameplay/gameplay_module.md)

## Objective

Move authored scene content out of `config/bootstrap.json` into a versioned,
Asset-owned level file. Bootstrap should select a startup level; it should not
be the long-term owner of mesh, material, light, camera, environment, or object
transform authoring.

This stage must also produce reusable validation levels so point-shadow and
later spot-shadow verification do not require repeatedly editing bootstrap
configuration.

## Entry condition

Begin GP7 after the current deferred-renderer D6/D7 verification handoff is
closed or explicitly paused with its evidence recorded. Preserve the working
point-shadow fixture until that verification is complete; migration must not
erase the only known-good visual test setup.

## Current state

The following was the GP7 entry baseline and is retained here to explain the
migration scope; it is no longer the live implementation:

- The original `config/bootstrap.json` contained both startup policy and an
  authored `scene` block, including explicit preload paths, models, materials,
  transforms, environment settings, and punctual lights.
- The original bootstrap parser produced `BootstrapScene` values. Runtime then
  created Gameplay Actors through focused factories, while some environment and
  startup-renderable information was handed directly to Render.

The landed GP7.1–GP7.5 path now loads a versioned `LevelResource` through
Asset, instantiates it through Runtime's transactional `LevelInstance`, and
publishes copied Gameplay sources to Render. Bootstrap selects only the
Asset-root-relative startup level. The final GP7.5 lifecycle and dual-backend
evidence pass is recorded in the GP7 journal.
- `AssetManager` already owns CPU asset identity and maintains forward and
  reverse dependency edges declared by loaders.
- `GameplayWorld` owns live Actors. Actor components publish copied,
  generational mesh, light, and camera source values to Render-owned
  registries; Gameplay never owns a render proxy or GPU object.
- There is no general reflection serializer, prefab system, or streaming
  consumer today; those remain outside GP7.4 scope.

## Design boundary and concrete question

The design question is: how can one authored file instantiate the current
static-mesh, light, camera, and environment scene while preserving existing
Asset, Gameplay, Render, and Graphics ownership?

The first answer is one CPU-only `LevelResource` loaded through Asset and one
Runtime-owned level instance that creates Actors in `GameplayWorld`. Do not add
a separate `WorldResource` yet. A world document becomes justified only when
there is a real consumer for multiple levels, streaming, transitions, or world
composition; until then bootstrap's `startup_level` identifies one level.

```text
bootstrap.json --startup_level--> AssetManager --> LevelResource
                                             dependencies --> model/material/environment assets
                                                    |
                                                    v
                                      Runtime level instance
                                         |          |
                              GameplayWorld Actors  environment source
                                         |          |
                              copied mesh/light/camera/environment values
                                                    |
                                                    v
                                             Render-owned state
                                                    |
                                                    v
                                               Graphics/RHI
```

## Scope

### Level authoring contract

Introduce a versioned `*.level` authoring format and an Asset payload for its
CPU representation. Version 1 uses a closed, validated set of records rather
than serialized C++ class names or an arbitrary property bag:

- stable authored object ID and optional human-readable name;
- local transform;
- static-mesh record with model asset reference, material asset reference,
  visibility, and `casts_shadow`;
- directional-, point-, and spot-light records using the existing Gameplay
  light value contracts, including `casts_shadow`;
- camera record using the existing camera lens/priority values;
- one level environment record with environment asset reference and IBL
  intensity.

The loader resolves referenced paths through Asset, records their `AssetID`s
as dependencies of the level asset, rejects duplicate object IDs and invalid
values, and reports path-qualified diagnostics. Authored files contain no
`ActorHandle`, source token, render proxy, descriptor, GPU handle, or backend
type.

### Instantiation and lifetime

Runtime owns a focused level-instance object. It retains the loaded level
identity and maps authored object IDs to the `ActorHandle`s created through
existing Gameplay factories. `GameplayWorld` remains the sole Actor owner.

Instantiation is transactional for the first slice: a required dependency or
object creation failure destroys everything created by that attempt and does
not publish a partially active level. Unload retires camera, light, mesh, and
environment sources before releasing level/dependency assets. The exact class
name is intentionally left to implementation, but this ownership is not.

Mesh, light, and camera records instantiate through their existing factories
and components. The level-wide environment is not forced into an Actor solely
for uniformity; Runtime publishes a copied environment-source value through a
narrow Render-owned registry or sink matching the current source-token model.
Render resolves its private environment resources at a frame boundary.

### Bootstrap and validation assets

Reduce bootstrap scene policy to a startup-level reference, for example:

```json
{
  "version": 2,
  "startup_level": "level/pbr_showcase.level"
}
```

Keep only genuinely process-global boot settings in bootstrap. Do not retain a
second manual list of every dependency already declared by the selected level.

Create at least these authored fixtures during migration:

- `pbr_showcase.level` for the normal deferred-PBR scene;
- `point_shadow_validation.level` for bounded six-face point-shadow proof;
- `spot_shadow_validation.level` before spotlight-shadow implementation or
  verification begins.

## Non-goals

- Editor level creation, save-back, undo/redo, or property inspection.
- Prefabs, scene inheritance, arbitrary reflected components, scripting, or
  cross-Actor references.
- World partition, level streaming, asynchronous activation, hot reload, or
  seamless travel.
- A separate `WorldResource` before multiple-level composition has a concrete
  runtime consumer.
- Binary cooking, package formats, or a general serialization framework.
- Moving render policy, render proxies, GPU resources, or backend objects into
  Asset or Gameplay.

## Invariants

- Asset owns level identity, immutable CPU authoring data, and dependency
  edges; it owns no GPU resource or live Actor.
- Runtime owns the level instance and authored-ID-to-ActorHandle mapping;
  `GameplayWorld` remains the sole owner of Actor memory and tick order.
- Render receives copied source values only and owns selection, proxy, pass,
  target, material-resolution, and environment-resolution policy.
- Graphics/RHI alone owns GPU resources, synchronization, backend translation,
  and safe destruction.
- Runtime must not gain an Editor dependency, and common contracts expose no
  Vulkan or OpenGL type.
- Level unload completes source retirement before RenderSystem teardown and
  before dependency eviction is attempted.
- Stable authored IDs survive file reorder and are distinct from transient,
  generational `ActorHandle`s.

## Stages

1. **GP7.1 — Asset schema and dependency graph.** Follow the concrete
   [GP7.1 stage plan](../../docs/gameplay/.plan/GP7.1.md). Define the versioned
   `LevelResource`, extension/type dispatch, parser validation, relative path
   rules, and dependency registration. Add loader tests for valid content,
   invalid versions/types/values, duplicate IDs, missing dependencies, dedup,
   and unload blocking while a level references a dependency.
2. **GP7.2 — Static-mesh instantiation and rollback.** Follow the concrete
   [GP7.2 stage plan](../../docs/gameplay/.plan/GP7.2.md). Add the Runtime-owned
   level instance, create static-mesh Actors through existing factories, retain
   the authored-ID map, and prove transactional failure plus deterministic
   unload/stale-handle behavior in headless tests.
3. **GP7.3 — Light, camera, and environment sources.** Follow the concrete
   [GP7.3 stage plan](../../docs/gameplay/.plan/GP7.3.md). Instantiate existing
   Gameplay light/camera compositions, add only the narrow environment source
   seam that has a current Render consumer, and verify source replacement and
   retirement at frame boundaries.
4. **GP7.4 — Bootstrap migration and fixtures.** Follow the concrete
   [GP7.4 stage plan](../../docs/gameplay/.plan/GP7.4.md). Close transitive
   material dependencies, replace the bootstrap `scene` and duplicated preload
   list with `startup_level`, author the PBR/point/spot levels, and remove
   transitional bootstrap scene plumbing once no consumer remains.
5. **GP7.5 — Runtime evidence and handoff.** Follow the concrete
   [GP7.5 stage plan](../../docs/gameplay/.plan/GP7.5.md). Run the final
   lifecycle/dependency audit and dual-backend smoke/capture validation. Record
   exact commands, captures, visual findings, skipped checks, and remaining
   risk in the GP7 journal before marking GP7 complete.

## Acceptance criteria

- [x] Bootstrap selects a startup level and no longer authors scene objects,
  lights, camera, materials, transforms, or environment settings.
- [x] Loading a level declares all referenced Asset dependencies without a
  duplicated manual preload list.
- [x] The current PBR scene is reproduced from `pbr_showcase.level` through
  Gameplay-owned Actors and copied render-source boundaries.
- [x] Point-shadow verification can select `point_shadow_validation.level`
  without editing the level contents or restoring bootstrap scene fields.
- [x] A spotlight-shadow fixture exists before that renderer stage begins.
- [x] Failed instantiation leaves no partial Actors or live source tokens.
- [x] Unload retires all level-created sources and Actors, rejects stale
  handles/tokens, and releases dependencies only when the Asset graph permits.
- [x] No serialized file or common header exposes a render proxy, GPU handle,
  descriptor, or backend-native type.
- [x] Vulkan and OpenGL render equivalent authored content, with visual capture
  evidence for the PBR showcase and point-shadow validation level.

## Validation plan

Follow [the validation matrix](../../docs/validation_matrix.md). Expected
implementation evidence is:

1. Level 1 targeted Asset, Gameplay, Runtime, and Render builds as their paths
   change.
2. Level 2 loader/dependency, GameplayWorld/level-instance, and render-source
   registry contract tests.
3. Level 3 `GraphicsSmoke` on Vulkan and OpenGL, plus command-registry captures
   under `save/screenshots/validation/` for the showcase and point-shadow
   levels.
4. Teardown under validation/debug layers with no live source, dependency, or
   GPU lifetime error.

Compilation alone is not completion because this changes runtime scene
lifetime and rendered output.

## Reference gate

- [Piccolo `Level`](https://github.com/BoomingTech/Piccolo/blob/main/engine/source/runtime/function/framework/level/level.h)
  owns a stable map of runtime game objects and exposes explicit load/unload.
  Its [implementation](https://github.com/BoomingTech/Piccolo/blob/main/engine/source/runtime/function/framework/level/level.cpp)
  loads a CPU `LevelRes`, creates runtime objects from its records, and clears
  them on unload. KimPeanut adopts the authored-resource-to-runtime-instance
  split and explicit instance lifetime, but keeps Actor ownership in the
  existing `GameplayWorld` and rejects Piccolo's global-manager coupling.
- [Godot `PackedScene`](https://github.com/godotengine/godot/blob/master/scene/resources/packed_scene.cpp)
  separates a serialized Resource from the node hierarchy created by
  `instantiate()`. KimPeanut adopts the two-phase load/instantiate idea, but
  deliberately rejects arbitrary reflected class construction, property bags,
  scene inheritance, and editor save semantics for GP7.

These references support the ownership shape; neither is a source template to
copy.

## Risks and resolved questions

- **Resolved for GP7.1:** level references are asset-root-relative, normalized,
  typed paths. They are not relative to the level file and may not escape the
  asset root.
- **Resolved for GP7.1:** environment and camera records are optional. Their
  absence publishes no source; staged Runtime fallback policy remains outside
  the Asset schema. Present model/material/environment dependencies fail
  deterministically when invalid or missing.
- AssetManager currently serializes loads. A level dependency fan-out may make
  load latency visible, but parallel loading is a separate measured task.
- The current bootstrap camera/environment fallback must remain valid during
  staged migration and be removed only after every replacement source has a
  tested empty/failure fallback.
- A future `WorldResource` must define ownership across multiple level
  instances, transitions, and streaming before it is introduced.
