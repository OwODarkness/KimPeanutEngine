# GP7.4 Review — Startup-Level Migration and Validation Fixtures

- Task ID: `GP7.4`
- Plan: [GP7.4 — Startup-Level Migration and Validation Fixtures](../.plan/GP7.4.md)
- Parent roadmap: [Gameplay TODO](../TODO.md#gp7--level-asset-and-startup-scene-migration)
- Review status: resolved; follow-up audit remains in GP7.5
- Latest review: 2026-09-02

## Scope and baseline

This review covers the current GP7.4 working-tree implementation: Bootstrap V2,
startup-level loading and dependency closure, material dependency identity,
Runtime/render-thread startup synchronization, camera selection and possession,
removal of the transitional Render bootstrap scene, checked-in fixtures, focused
tests, and recorded runtime/capture evidence.

GP7.3's six review findings are resolved in the current baseline. GP7.5 remains
the final audit, but it cannot absorb acceptance criteria that GP7.4 explicitly
requires before this stage is complete.

## Evidence inspected

- `engine/runtime/bootstrap/bootstrap.h`
- `engine/runtime/bootstrap/bootstrap.cpp`
- `engine/runtime/asset/utility.h`
- `engine/runtime/asset/level_loader.cpp`
- `engine/runtime/asset/material.h`
- `engine/runtime/asset/material_loader.cpp`
- `engine/runtime/render/material/material_asset_resolver.h`
- `engine/runtime/render/material/material_asset_resolver.cpp`
- `engine/runtime/engine.h`
- `engine/runtime/engine.cpp`
- `engine/runtime/runtime_global_context.h`
- `engine/runtime/runtime_global_context.cpp`
- `engine/runtime/level/level_instance.h`
- `engine/runtime/level/level_instance.cpp`
- `engine/runtime/render/camera_source.h`
- `engine/runtime/render/camera_source_registry.cpp`
- `engine/runtime/render/render_system.h`
- `engine/runtime/render/render_system.cpp`
- Bootstrap, Asset, RuntimeLevel, material resolver, environment, and fixture
  tests
- all three checked-in Level fixtures and the fourteen recorded GP7.4 captures
- GP7.4 plan, parent spec, Gameplay/Render documentation, status, journal, and
  validation matrix

## Review round 1 — 2026-09-02

### F1 — the two-phase startup handshake is not exception-total

- Priority: P1
- Location: `engine/runtime/engine.cpp:107-123, 299-329`
- Status: resolved in follow-up

`FinalizeGameStartup()` is called without a guard that always publishes an
Abort decision. If level/factory, input-context allocation, controller, or
another game-side operation throws, `Engine::Initialize()` unwinds while the
render thread remains blocked forever on `startup_decision_cv_`; the still
joinable thread can then terminate the process during destruction. The RAII
rollback inside `LevelInstance` does not signal this condition variable.

The render-thread `try` block also ends before the commit wait and
`InitEditorUI()`. An exception while initializing UI or entering the frame loop
therefore escapes the thread function through `std::terminate`, despite GP7.4's
explicit exception-propagation requirement. Make Abort publication/join an
unconditional scope-owned operation on every game-side exit, and contain the
whole render thread entry point with a defined post-Ready failure policy.

### F2 — no Runtime startup tests exercise the new synchronization boundary

- Priority: P1
- Location: `engine/test/unit/CMakeLists.txt:11-13`
- Status: resolved in follow-up

There is no Engine/Runtime startup test target and no test references
`FinalizeGameStartup`, the ready/commit/abort handshake, or its diagnostics.
Consequently the required valid commit, camera-free/stale/wrong-type level,
instantiation/controller/possession failures, render initialization exceptions,
abort wake/join behavior, and repeated one-shot state are all untested. The
current 48 focused tests cover the components below this orchestration layer,
not the GP7.4 transaction itself. Add explicit injectable startup seams and a
headless synchronization test target.

### F3 — the validation fixtures do not produce meaningful shadow diagnostics

- Priority: P1
- Location: `asset/level/point_shadow_validation.level`,
  `asset/level/spot_shadow_validation.level`
- Status: resolved in follow-up

The GP7.4 plan requires the point and spot fixtures to make their depth and
visibility views meaningful. Visual inspection of all recorded Vulkan/OpenGL
diagnostic captures instead shows uniform white depth output and almost wholly
white visibility output with only a few isolated dark pixels. The execution
journal acknowledges these captures are uniform or near-uniform and defers
interpretation to GP7.5, while the GP7.4 acceptance boxes remain checked.

Adjust the fixtures and/or diagnostic capture path until caster, receiver,
coverage, and occlusion are visibly distinguishable on both backends, then
replace the evidence. GP7.5 may audit that proof, but should not be used to
create evidence GP7.4 says already exists.

### F4 — the advertised fresh initialize/clear cycle dereferences a cleared context

- Priority: P2
- Location: `engine/runtime/engine.cpp:178-192`,
  `engine/runtime/runtime_global_context.cpp:232-267`
- Status: resolved in follow-up

`Engine::Clear()` resets `startup_level_loaded_` with the comment that a fresh
engine cycle can reload the selection. However, `RuntimeContext::Clear()`
permanently resets the global window, RenderSystem, GameplayWorld,
LevelInstance, input, Lua, log, and other owned systems. A subsequent
`Initialize()` starts a render thread that immediately dereferences the null
`window_system_`. Either reconstruct the Runtime composition root for the
planned repeated-cycle case and test it, or remove the misleading reset and
state explicitly that the process is terminal after clear.

### F5 — material-relative dependency resolution accepts absolute and escaping paths

- Priority: P2
- Location: `engine/runtime/asset/utility.h:196-215`
- Status: resolved in follow-up

`ResolveOwnedAssetPath()` accepts an absolute authored reference verbatim and
allows `..` segments to escape the Asset root after lexical normalization. A
checked-in material can therefore load a shader or texture from an arbitrary
host path, making the supposedly complete Asset-owned dependency graph
machine-dependent and bypassing the root-relative policy used by levels and
bootstrap. Reject absolute/drive-qualified references and verify that the
resolved material-relative path remains beneath the Asset root; add absolute
and multi-parent escape tests for both shader and texture fields.

### F6 — the parent spec still describes the removed bootstrap scene as current

- Priority: P3
- Location: `.spec/specs/gameplay-level-asset.md:3, 26-40`
- Status: resolved in follow-up

The parent spec remains `proposed` and its current-state section says
`config/bootstrap.json` owns a scene/preload manifest, produces
`BootstrapScene`, and that no level asset or level-instance object exists.
Those statements contradict the GP7.4 implementation and current module/status
documents. Update the spec status/current-state section or clearly label it as
the historical entry baseline so future work does not reintroduce removed
plumbing.

## Review round 2 — 2026-09-02

All six findings from round 1 are resolved in the current working tree:

- F1: `Engine` arms an RAII abort/join guard immediately after creating the
  render thread. `AbortStartupTransaction()` publishes Abort, wakes frame
  waits, and joins. The full render entry point catches startup, UI, and frame
  loop exceptions, closes UI best-effort, and clears Runtime exactly once.
  Commit is published only after potentially throwing command transport setup.
- F2: `RuntimeStartupTest` is registered as a dedicated headless target and
  covers valid commit, camera-free, stale/wrong-type IDs, controller/possession
  rejection, and terminal Clear lifecycle behavior. Lower-level typed Level
  failure and destructor cases remain covered by `RuntimeLevelTest`.
- F3: Punctual depth capture now linearizes depth using each shadow frame's
  near/far range. Point and spot fixtures place the source behind the receiver
  set. Fresh Vulkan captures show non-uniform depth and visibility silhouettes
  for both fixtures.
- F4: `Engine::Clear()` is explicitly terminal and rejects later Initialize;
  it also stops and joins an active render thread before clearing editor state.
- F5: material dependency resolution rejects absolute/rooted references and
  lexical paths that leave `GetAssetDirectory()`. Focused tests cover absolute
  shader, absolute texture, and multi-parent texture escape cases.
- F6: the parent spec now marks GP7.4 landed, labels the old bootstrap scene
  description as historical entry state, and identifies GP7.5 as the remaining
  lifecycle/evidence audit.

## Positive observations

- Bootstrap V2 is closed, validates one normalized startup-level reference,
  and no legacy `BootstrapScene` or request-builder symbol remains in Runtime.
- Startup level loading occurs before render-thread creation and gives Runtime
  one typed Level AssetID rather than a path or CPU payload.
- Material loading now declares shader and texture dependency requests, and the
  authored material resolver consumes dependency indices rather than authored
  paths or material-side `LoadSync` calls.
- Camera preference is expressed by one pure rule shared between Runtime's
  authored-order selection and Render's source-handle selection.
- Hard-coded Runtime scene Actors/lights and Render bootstrap environment state
  are removed; the checked-in PBR level is the selected startup fixture.
- Runtime still destroys LevelInstance and GameplayWorld before RenderSystem,
  preserving source-retirement ownership.

## Validation

Current-tree validation on 2026-09-02:

- `.\tools\kp.ps1 build RuntimeLib` — passed
- `ctest --test-dir build -C Debug --output-on-failure -R
  "BootstrapTest|MaterialLoaderTest|LevelLoaderTest|MaterialAssetResolverTest|RuntimeLevelTest|RenderSystemEnvironmentTest"`
  — passed 48/48 in the prior review; the follow-up focused selection passed 36/36
- inspected the fresh Vulkan point/spot scene, depth, and visibility captures;
  the punctual depth and visibility views now show non-uniform caster/receiver
  silhouettes

The review inspected the recorded full build, full CTest, dual-backend smoke,
and capture commands in the journal but did not repeat the complete suite or
OpenGL live capture cycle. GP7.5 should repeat the complete matrix and audit
cross-backend parity.
