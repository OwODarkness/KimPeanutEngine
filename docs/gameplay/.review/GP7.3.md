# GP7.3 Review — Level Lights, Cameras, and Environment Source

- Task ID: `GP7.3`
- Plan: [GP7.3 — Level Lights, Cameras, and Environment Source](../.plan/GP7.3.md)
- Parent roadmap: [Gameplay TODO](../TODO.md#gp7--level-asset-and-startup-scene-migration)
- Review status: resolved; acceptance matrix complete
- Latest review: 2026-09-02

## Scope and baseline

This review covers the current GP7.3 working-tree implementation: the complete
V1 typed Actor factory transaction, neutral camera composition, optional level
environment preflight/registration, the Render-owned single-source registry,
frame-boundary environment resolution and fallback, Runtime wiring, shutdown
ordering, focused tests, and the recorded validation evidence.

GP7.4 startup-level migration remains out of scope. GP7.3 intentionally leaves
the bootstrap scene as the live production startup path.

## Evidence inspected

- `engine/runtime/level/level_instance.h`
- `engine/runtime/level/level_instance.cpp`
- `engine/runtime/gameplay/factory/camera_actor_factory.h`
- `engine/runtime/gameplay/factory/camera_actor_factory.cpp`
- `engine/runtime/render/environment_source.h`
- `engine/runtime/render/environment_source_registry.h`
- `engine/runtime/render/environment_source_registry.cpp`
- `engine/runtime/render/render_system.h`
- `engine/runtime/render/render_system.cpp`
- `engine/runtime/runtime_global_context.h`
- `engine/runtime/runtime_global_context.cpp`
- focused RuntimeLevel, Gameplay, Render registry, and RenderSystem tests
- GP7.3 plan, parent GP7 spec, Gameplay/Render module documentation, status,
  journal, and validation matrix

## Review round 1 — 2026-09-02

### F1 — `Clear` can revalidate a stale environment handle

- Priority: P1
- Location: `engine/runtime/render/environment_source_registry.cpp:54-63`
- Status: resolved in review round 2

`Clear()` replaces the complete `HandleSystem` with a new empty instance. The
next create therefore returns `{id = 0, generation = 0}` again. A caller that
retained the pre-clear `{0, 0}` token can then pass the live-handle equality and
generation checks and destroy the replacement source. This violates GP7.3's
opaque generational ownership and stale-destroy guarantee.

`Clear()` now destroys the live handle while retaining the `HandleSystem`
generation history. `ClearInvalidatesOldHandleGeneration` proves a replacement
cannot be destroyed by the pre-clear token.

### F2 — queue publication is not exception-safe

- Priority: P2
- Location: `engine/runtime/render/environment_source_registry.cpp:25-27, 34-40`
- Status: resolved in review round 2

`EnqueueCreate()` now publishes ownership only after command insertion succeeds,
and rolls back the allocated handle if insertion throws. `EnqueueDestroy()`
queues first, then destroys the handle; its catch path removes only a command
that was actually appended. `HandleSystem::Destroy()` also publishes the free
slot before advancing the generation, so allocation failure cannot partially
mutate the token state.

### F3 — the central Render failure/fallback contract is untested

- Priority: P1
- Location: `engine/test/unit/render/render_system_test.cpp:429-474`
- Status: resolved in review round 2

The injected-backend matrix now observes the deferred-lighting environment
bindings and covers bootstrap retention, black-baseline retention, atomic
replacement, destroy-to-baseline, wrong format, malformed data, processing /
texture-creation failure, one-diagnostic suppression, and shutdown handle
clearing.

### F4 — RuntimeLevel does not exercise the required failure matrix

- Priority: P2
- Location: `engine/test/unit/runtime_level/runtime_level_test.cpp:820-910`
- Status: resolved in review round 2

Focused RuntimeLevel cases now cover point, spot, and camera factory failures
with immediate retry, missing and occupied environment sinks after Actor
creation, and environment-only success. Existing directional and mixed-actor
cases remain in the same matrix.

### F5 — Actor factory diagnostics omit the authored kind

- Priority: P2
- Location: `engine/runtime/level/level_instance.cpp:215-220`
- Status: resolved in review round 2

`PendingActor` retains a stable authored-kind label and actor creation failures
now report both that kind and the authored ID. Point, spot, and camera retry
tests assert the kind appears in the diagnostic.

### F6 — the environment binding bundle is public module API

- Priority: P3
- Location: `engine/runtime/render/render_system.h:125-144`
- Status: resolved in review round 2

`EnvironmentBindingBundle` is now nested in `RenderSystem`'s private section;
the public Render contract exposes only the source sink and lifecycle APIs.

## Review round 2 — 2026-09-02

All six round-1 findings are resolved in the working tree. The fixes preserve
the existing Runtime → Gameplay → Render ownership boundary and add no backend
or CPU-payload exposure to the environment source API.

Validation added for this round:

- `cmake --build build --config Debug --target RenderSystemTest RuntimeLevelTest`
  — passed
- `ctest --test-dir build -C Debug -R
  "RenderSystemEnvironmentTest|EnvironmentSourceRegistryTest"`
  — passed 8/8
- `ctest --test-dir build -C Debug -R
  "RuntimeLevelTest\\.(PointFactory|SpotFactory|CameraFactory|MissingEnvironmentSink|OccupiedEnvironmentSink|InstantiatesEnvironmentOnlyLevel)"`
  — passed 6/6

The targeted build required elevated host access because the sandbox could not
read the installed Windows SDK probe directory. The complete suite and backend
smoke/capture evidence remain the broader project validation gate, not an open
GP7.3 finding.

## Positive observations

- `LevelInstance` preflights all authored objects before mutation, creates
  Actors in authored order, registers environment last, and uses an RAII guard
  for reverse rollback on returns and exceptions.
- The environment source description remains value-only and contains no path,
  `LevelResource`, CPU payload pointer, Gameplay object, or backend handle.
- Render resolves the ready AssetID at `BeginFrame`, builds a temporary bundle,
  and reuses the retained derived bundle for intensity-only re-registration.
- Runtime keeps the intended LevelInstance -> GameplayWorld -> RenderSystem
  destruction direction, and Render clears source registries before resolver
  and backend cleanup.
- The neutral camera rename leaves no duplicate `CreateFreeCameraActor` API.

## Validation

Current-tree validation on 2026-09-02:

- `.\tools\kp.ps1 build RenderPassScheduleTest` — passed
- `.\tools\kp.ps1 build RenderSystemTest` — passed
- `.\tools\kp.ps1 build RuntimeLevelTest` — passed
- `ctest --test-dir build -C Debug --output-on-failure -R
  "EnvironmentSourceRegistryTest|RenderSystemEnvironmentTest|RuntimeLevelTest"`
  — passed 33/33
- `cmake --build build --config Debug` — passed
- `ctest --test-dir build -C Debug --output-on-failure` — passed 231/231
- `build/engine/example/graphics/Debug/GraphicsSmoke.exe` — passed

The first sandboxed build attempt failed before compilation because MSBuild
could not read the installed Windows SDK directory; rerunning with the required
host permission passed. Dual-backend smoke runs and SceneColor captures were
not repeated in this pass; the direct smoke executable exercised both APIs and
reported `Graphics smoke (3 frames/API): passed`. The `kp.ps1 smoke` wrapper
itself still has an unrelated empty-argument binding defect; its build phase
passed before the direct invocation was used.
