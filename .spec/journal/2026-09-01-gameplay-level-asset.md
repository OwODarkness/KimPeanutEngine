# GP7.1 Level Asset — 2026-09-01

## Objective

Implement the [GP7.1 stage plan](../../docs/gameplay/.plan/GP7.1.md): add the
closed V1 CPU level asset format and manager-owned dependency transaction while
leaving Runtime, Gameplay, Render, bootstrap, and live scene behavior unchanged.

## Changes

- Added `KPAT_Level`, `LevelResource`, `LevelPtr`, five closed object record
  variants, optional environment data, typed normalized references, and
  dependency-request metadata under `engine/runtime/asset/`.
- Added `LevelLoader` with strict field, type, finite-value, transform, light,
  camera, cone, and asset-root-relative path validation. It has no
  `AssetManager`, Gameplay, Render, or Graphics dependency.
- Extended `AssetManager::LoadSync` to resolve declared requests after
  `load_mutex_` is released, then register all resolved edges together. Added
  checked `(owner, dependency index, expected type)` lookup and shared
  canonical path-key normalization.
- Added generated-fixture tests for complete records, deduplication,
  normalization/rejection, invalid values, missing dependency failure,
  reverse-edge lifetime, stale lookup, and concurrent same-level loading.

## Reference gate

The GP7 spec's existing Piccolo `Level` and Godot `PackedScene` findings were
used: serialized CPU authoring data stays separate from runtime instantiation,
and centralized asset coordination owns dependency identity/lifetime. No
reference source was copied.

## Validation

- `.\tools\kp.ps1 build AssetUnitTest` — passed after adding the existing
  `Config` interface dependency required by `config/path.h`.
- `AssetUnitTest.exe --gtest_color=no` — 14/14 passed.
- `cmake --build build --config Debug` — passed.
- `ctest --test-dir build -C Debug --output-on-failure` — 197/197 passed.
- Runtime smoke and image capture were intentionally skipped: GP7.1 changes
  no runtime scene or rendering path.

## Remaining risk

GP7.2 must define deterministic model-to-mesh selection and consume the
checked dependency lookup. Bootstrap migration, Actor creation, source
publication, and runtime visual validation remain GP7.2–GP7.5 work.

## Review correction — 2026-09-01

A follow-up code review reopened GP7.1. The implementation boundary is sound,
but completion is blocked by these findings:

1. Resolved dependencies are not revalidated under the final Asset state lock.
   A concurrent unregister can therefore remove a dependency before parent
   registration, producing a level with a stale dependency ID and no reverse
   edge.
2. The level schema represents `lod_bias` as `float` although its existing
   Gameplay and Render consumers use `int`.
3. Some closed-schema and missing-required-string failures do not emit the
   promised path-qualified diagnostic.
4. The named deduplication test contains no repeated reference, so that
   acceptance behavior is not covered.

The existing `AssetUnitTest` executable still passed 14/14 during review. A
fresh `AssetUnitTest` rebuild could not be independently completed because
MSBuild was denied access to
`C:\Users\17519\AppData\Local\Microsoft SDKs` while evaluating the Windows SDK
version (`MSB4184`). This is an environment failure, not a source failure, but
the fixes require a rebuilt focused suite before GP7.1 can be closed again.

## Review resolution — 2026-09-01

The four review findings were resolved:

1. `AssetManager::LoadSync` now revalidates every loader-declared dependency
   under the final `state_mutex_` immediately before parent registration.
2. `LevelStaticMeshRecord::lod_bias` and its parser now use `int`.
3. Closed-field and required-string failures now emit path-qualified loader
   diagnostics, including missing `kind` and camera `projection`.
4. The focused level test repeats mesh references and verifies shared indices
   and unique dependency registration.

Validation after the fixes: rebuilt `AssetUnitTest` passed 14/14; the full
Debug build passed; and the complete CTest suite passed 197/197. GP7.1 is
closed. Runtime smoke and image capture remain deferred because this stage is
Asset-only; runtime level instantiation begins in GP7.2.

## GP7.2 execution — 2026-09-01

Implemented the Runtime-owned static-mesh level instance from
[GP7.2](../../docs/gameplay/.plan/GP7.2.md):

- Added the `RuntimeLevel` target and non-copyable `LevelInstance` with typed
  result errors, Asset-only preflight, explicit model `KPMG_Mesh` resolution,
  validated mesh/material payloads, authored-order Actor creation, stable
  authored-ID lookup, reverse rollback, idempotent unload, and destructor
  cleanup.
- Added `GameplayWorld::ReclaimDestroyedActors()` as the explicit game-thread
  reclamation boundary. `DestroyActor` still invalidates lookup immediately,
  but handle slots are not reusable until owned destroyed storage is reclaimed.
- Added dormant `RuntimeContext::level_instance_` ownership and explicit reset
  before GameplayWorld/Render teardown. Bootstrap scene creation and the V1
  LevelResource schema remain unchanged.
- Added focused RuntimeLevel tests for mapping/order, skipped GP7.3 records,
  preflight rejection, reverse rollback with immediate retry, stale lookup,
  active replacement rejection, idempotent unload, Asset residency, and source
  create/destroy balance. GameplayWorld now independently tests reclamation.

Validation after implementation: `RuntimeLevelTest` passed 6/6, the focused
`GameplayWorldTest` set passed 19/19, the full Debug build passed, and complete
CTest passed 204/204. No Vulkan/OpenGL smoke or image capture was run because
GP7.2 does not enter the live bootstrap path; those checks remain GP7.4/GP7.5
acceptance work.
