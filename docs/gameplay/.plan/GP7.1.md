# GP7.1 — Level Asset Schema and Dependency Graph

- Status: complete (2026-09-01; review risks resolved)
- Parent spec: [Gameplay Level Asset (GP7)](../../../.spec/specs/gameplay-level-asset.md)
- Parent roadmap: [Gameplay TODO](../TODO.md#gp7--level-asset-and-startup-scene-migration)
- Affected owner: Asset
- Future consumers: Runtime level instance (GP7.2), Gameplay factories (GP7.2–GP7.3)

## Outcome

Add one versioned, CPU-only `*.level` asset whose parsed records describe the
current static-mesh, light, camera, and environment authoring values. Loading a
level must load and retain its model, material, and environment dependencies
through the existing Asset graph without creating Actors, render sources, or GPU
resources.

GP7.1 ends at the Asset boundary. It does not migrate bootstrap or instantiate
the level.

## Entry condition

Render R1.1 characterization and D6 verification are complete. Preserve their
fixtures and behavior while this Asset-only stage lands. GP7.1 must not change
`RenderSystem`, bootstrap scene creation, or the live startup scene.

## Design decisions

### One closed V1 schema

Version 1 is defined completely in GP7.1, including records that Runtime will
not instantiate until GP7.2 and GP7.3. Do not mutate the meaning of V1 between
those stages.

The file is a closed discriminated schema, not reflected C++ serialization. A
level contains:

- integer `version`, exactly `1`;
- optional level-wide `environment`;
- an `objects` array;
- for every object, a non-empty unique stable `id`, optional display `name`, a
  recognized `kind`, and only the fields allowed by that kind.

Supported object kinds and value fields are:

| Kind | Required values | Optional values and defaults |
|---|---|---|
| `static_mesh` | `transform`, `model`, `material` | `visible=true`, `casts_shadow=true`, `lod_bias=0` |
| `directional_light` | `direction`, `color`, `intensity` | `enabled=true`, `casts_shadow=true` |
| `point_light` | `position`, `color`, `intensity`, `range` | `enabled=true`, `casts_shadow=true` |
| `spot_light` | `position`, `direction`, `color`, `intensity`, `range`, `inner_cone_radians`, `outer_cone_radians` | `enabled=true`, `casts_shadow=true` |
| `camera` | `transform`, `projection`, `near_plane`, `far_plane` | `field_of_view_degrees=45`, `orthographic_height=10`, `enabled=true`, `priority=0` |

`transform` contains `position`, Euler `rotation_degrees`, and `scale`, each a
three-number array. `projection` is `perspective` or `orthographic`.

The optional environment contains `texture` and `ibl_intensity`. Omitting it
means that this level publishes no environment source later; it does not create
an implicit Asset dependency. Omitting all camera records is also valid so the
staged migration can retain the existing Runtime fallback until GP7.4.

An illustrative file is:

```json
{
  "version": 1,
  "environment": {
    "texture": "texture/hdr/HDR_041_Path.hdr",
    "ibl_intensity": 0.25
  },
  "objects": [
    {
      "id": "showcase.floor",
      "name": "Floor",
      "kind": "static_mesh",
      "transform": {
        "position": [0.0, -70.0, 0.0],
        "rotation_degrees": [0.0, 0.0, 0.0],
        "scale": [30.0, 30.0, 30.0]
      },
      "model": "model/brickwall/floor.obj",
      "material": "material/oakfloor_pbr.material",
      "visible": true,
      "casts_shadow": true
    }
  ]
}
```

Do not serialize class names, component type names, `ActorHandle`, source
tokens, `AssetID`, render handles, descriptors, or backend-native values.

### Asset-root-relative references

Every reference inside a level is relative to `GetAssetDirectory()`, using
forward slashes, for example `model/sphere/sphere.obj`. Moving a `.level` file
must not silently retarget its dependencies.

Normalization must:

1. reject empty, absolute, drive-qualified, and rooted paths;
2. normalize separators and `.` segments;
3. reject any `..` traversal that escapes or aliases outside the asset root;
4. derive the expected `AssetType` from the normalized extension;
5. reject a type mismatch before registration;
6. use the same case-folded key policy as `AssetManager` for deduplication.

Models must resolve as `KPAT_Model`, materials as `KPAT_Material`, and the V1
environment must be an HDR `KPAT_Texture`. The normalized authored path remains
in `LevelResource` for diagnostics and future editor display; serialized files
never contain process-local IDs.

### CPU representation

Add `LevelResource` under `engine/runtime/asset/`, with API-neutral source
records matching the table above. Use a `std::variant` for the five object
record kinds and preserve file order for deterministic later instantiation.

Each authored asset reference stores:

- its normalized asset-root-relative path;
- its expected `AssetType`;
- an index into the owning Asset wrapper's ordered dependency vector.

The payload remains immutable authoring data. The resolved `AssetID`s remain in
`Asset::dependencies`, where lifetime and reverse edges already belong. Add a
small read-only AssetManager query that resolves `(owner AssetID, dependency
index, expected type)` to a still-valid dependency ID; GP7.2 must use this query
instead of loading paths again.

Repeated references share one dependency index. Preserve first-appearance
order so indices and test diagnostics are deterministic.

### Parse-only loader, manager-owned dependency transaction

`LevelLoader` parses and validates the document and declares typed dependency
requests in `AssetRegisterInfo`. It must not hold or call `AssetManager`.

This rule is required by the current lock model: `LoadSync()` invokes shared
loaders while holding the non-recursive `load_mutex_`. Calling public
`LoadSync()` from a loader would self-deadlock.

Extend the normal top-level load pipeline as follows:

```text
LoadSync(level path)
  -> cache lookup
  -> lock load_mutex_
       LevelLoader parses values and declares unique dependency requests
     unlock load_mutex_
  -> AssetManager resolves each request through normal LoadSync
  -> reject missing or wrong-type dependency
  -> lock state_mutex_
       recheck level cache
       bind ordered dependency IDs
       register level and add reverse edges once
     unlock state_mutex_
  -> return level AssetID
```

The dependency-resolution step must occur after releasing `load_mutex_` and
before registering the level. `AssetRegisterInfo::dependencies` is populated
only after all declared requests resolve successfully. Existing loaders with no
dependency requests retain their current behavior.

V1 deliberately permits dependencies only on model, material, and HDR texture
assets, never on another level. General recursive composite assets, cycle
detection, parallel dependency fan-out, and a new asynchronous loader context
are separate future designs; GP7.1 must not solve them speculatively.

### Failure and concurrency semantics

- A parse, normalization, missing-file, unsupported-type, or wrong-type failure
  returns an invalid level ID and registers no level or reverse dependency edge.
- If earlier dependencies loaded before a later dependency fails, those assets
  may remain as ordinary unreferenced cache entries. Do not attempt rollback
  deletion because an entry may predate the request or be shared concurrently.
- Concurrent requests for the same level may parse or resolve redundantly, but
  the existing final cache recheck must produce one registered level identity.
- Register all level dependency edges together after the final cache recheck.
- Unregistering a referenced dependency remains refused. Unregistering the
  level removes its reverse edges, after which unreferenced dependencies may be
  removed normally.
- Diagnostics identify the level path, object ID or environment field, source
  reference, and reason. Logging is sufficient for GP7.1; do not add a global
  diagnostic framework without another consumer.

## Validation rules

Reject the entire level for any of the following:

- missing, non-integer, or unsupported version;
- unknown top-level field, object kind, or kind-specific field;
- missing/empty/duplicate object ID;
- non-finite vector, transform, color, or scalar value;
- any zero scale component;
- a zero-length light direction;
- negative color, intensity, environment intensity, or LOD bias;
- non-positive point/spot range;
- spot cones outside `0 <= inner <= outer < pi/2`;
- camera FOV outside `[1, 179]`, non-positive near plane, or
  `far_plane <= near_plane`;
- non-positive orthographic height;
- malformed, escaping, missing, or wrong-type asset reference.

Normalize non-zero direction values only during GP7.2/GP7.3 instantiation; the
Asset payload preserves the validated authored values.

## Implementation slices

### GP7.1.1 — Types and dispatch

- Add `KPAT_Level`, `LevelPtr`, and `LevelResource` to the Asset type/payload
  contracts.
- Add `.level` extension recognition and `LevelLoader` construction/dispatch.
- Add the new sources explicitly to Asset and AssetUnitTest CMake lists.

### GP7.1.2 — Strict V1 parser

- Implement closed-field helpers and path-qualified validation.
- Parse the complete V1 environment and object variant schema.
- Build one deterministic, deduplicated dependency-request table and assign
  dependency indices to authored references.
- Keep the loader CPU-only and free of AssetManager, Gameplay, Render, and RHI
  dependencies.

### GP7.1.3 — Dependency coordinator

- Extend `AssetRegisterInfo` with typed unresolved dependency requests.
- Resolve them in `AssetManager::LoadSync()` only after `LoadByExtension()` has
  released `load_mutex_`.
- Validate result types, populate the ordered dependency IDs, and register the
  parent only after every request succeeds.
- Add the checked dependency-index lookup needed by GP7.2.

### GP7.1.4 — Contract tests and handoff

- Add focused level-loader and dependency-graph tests.
- Document actual implementation/validation evidence in a GP7 journal when
  implementation begins; keep this file as the durable design.
- Mark only GP7.1 complete. Do not create Actors or remove bootstrap fields in
  this stage.

## Required tests

Add fixtures generated in the test temporary directory; do not depend on the
large showcase assets for parser tests.

- minimal empty level and one complete record of every kind;
- optional environment/camera absence;
- unsupported versions, malformed JSON, unknown fields/kinds, missing fields,
  duplicate IDs, and every numeric boundary above;
- path separator normalization, `.` normalization, absolute/drive/traversal
  rejection, unsupported extension, missing file, and expected-type mismatch;
- repeated model/material/environment references produce one ordered dependency
  edge each and stable dependency indices;
- loading the same level twice returns the same ID;
- a failed dependency leaves no level asset and no reverse edge to the failed
  parent, including failure after one successful child load;
- a loaded level prevents dependency unregister; unregistering the level removes
  the reverse edge and permits normal dependency removal;
- checked dependency lookup rejects stale owner IDs, out-of-range indices, and
  wrong expected types;
- concurrent same-level `LoadSync` calls return one registered identity.

Because `AssetType`, `AssetPayload`, the public Asset registration contract,
and CMake wiring change, implementation validation is Level 4:

```powershell
.\tools\kp.ps1 build AssetUnitTest
.\tools\kp.ps1 test AssetUnitTest
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

No runtime smoke or image capture is required for GP7.1 because it changes no
runtime scene or rendering path. Those become mandatory in GP7.4–GP7.5.

## Acceptance criteria

- [x] A valid `*.level` loads as one `KPAT_Level` with the complete immutable V1
  CPU representation.
- [x] The loader accepts only the closed schema and emits path-qualified failure
  diagnostics.
- [x] Asset-root-relative references normalize safely and cannot escape the
  asset root.
- [x] All model, material, and present environment references are loaded once,
  type checked, indexed deterministically, and registered as level dependencies.
- [x] No loader calls AssetManager and dependency resolution never recurses
  while `load_mutex_` is held.
- [x] Failed loading registers no level and no partial reverse edges.
- [x] Dependency lifetime, deduplication, stale lookup, and concurrent same-path
  behavior are covered by focused tests.
- [x] Gameplay, Runtime, Render, Graphics, bootstrap, and live visual output are
  unchanged.

## Explicit non-goals

- Actor creation, authored-ID-to-`ActorHandle` maps, or transactional instance
  rollback (GP7.2).
- Light/camera/environment source publication (GP7.3).
- `startup_level`, bootstrap scene removal, or showcase/shadow fixtures (GP7.4).
- Editor save/load tools, reflection, arbitrary components, prefab inheritance,
  level-to-level references, streaming, hot reload, cooking, or `WorldResource`.
- Parallelizing AssetManager or replacing its complete lock/cache architecture.

## Reference findings

- [Bevy `LoadContext`](https://github.com/bevyengine/bevy/blob/main/crates/bevy_asset/src/loader.rs)
  records dependencies requested during a load and transfers them into the
  completed loaded asset. KimPeanut adopts manager-owned declaration and
  lifetime tracking, but not Bevy's async task system, typed handles, labeled
  sub-assets, or automatic nested-load machinery.
- [Godot `ResourceFormatLoader` and `ResourceLoader`](https://github.com/godotengine/godot/blob/master/core/io/resource_loader.h)
  separate format recognition/dependency reporting from centralized cache and
  load coordination. KimPeanut adopts that ownership separation, but not
  Godot's threaded task graph, UID remapping, cache modes, or editor-oriented
  dependency rewrite features.
- [Piccolo `Level::load`](https://github.com/BoomingTech/Piccolo/blob/main/engine/source/runtime/function/framework/level/level.cpp)
  loads a CPU `LevelRes` before creating runtime objects. It supports the wider
  GP7 resource/instance split, but its global-manager and immediate object-load
  behavior are not templates for GP7.1's Asset dependency transaction.

The transferable conclusion is narrow: the loader describes authored content
and its needs; the Asset coordinator resolves identity, cache reuse, and
lifetime. Runtime instantiation is a later owner and stage.

## Review risks resolved (2026-09-01)

- **Dependency registration race:** declared dependency requests are
  revalidated under `state_mutex_` immediately before `RegisterAsset`, so an
  unregister cannot leave a parent with a stale child ID or missing reverse
  edge.
- **LOD-bias schema alignment:** `lod_bias` is now authored and parsed as an
  integer, matching existing Gameplay and Render consumers.
- **Path-qualified diagnostics:** closed-field and required-string failures now
  use the loader diagnostic helper, including unknown kind fields, missing
  `kind`, and missing camera `projection`.
- **Deduplication coverage:** the focused level test repeats mesh references
  and verifies shared dependency indices plus the unique registered edges.

## Remaining risks handed to later stages

- `Asset::dependencies` is a vector and reverse-edge maintenance is linear;
  GP7.1 deduplicates requests but does not optimize the global graph without a
  measured scale problem.
- Dependencies load serially. Profile real level fan-out after GP7.4 before
  designing parallel loading.
- Model dependencies resolve to `ModelResource`, while the static-mesh factory
  consumes a mesh AssetID. GP7.2 must define deterministic model-to-mesh
  selection without making the level loader depend on Gameplay or Render.
- Material files still resolve their own shader/texture paths later in Render.
  GP7.1 records the material asset as the level dependency; extending Material
  Asset dependency declaration is a separate Asset task, not hidden GP7.1
  scope.
