# LO2 Review — Staged Runtime Startup and Editor Promotion

- Task ID: `LO2`
- Plan: [LO2 — Staged Runtime Startup and Editor Promotion](../.plan/LO2.md)
- Execution spec: [Asset Loading Progress](../../../.spec/specs/asset-loading-progress.md)
- Parent roadmap: [Asset Loading Progress TODO](../TODO.md)
- Review status: round 2 — implementation risks addressed; visual/LO3 evidence remains open
- Review date: 2026-09-04

## Scope and baseline

This review covers the current uncommitted LO2 implementation: the
Runtime-owned startup coordinator, presentation/scene capability split,
loading presentation loop, render and Editor promotion handshakes, partial
startup rollback, focused tests, and matching Asset/Editor/status documents.

LO1 observation internals and the LO3 snapshot-driven presentation are out of
scope except where LO2 incorrectly owns, terminates, or exposes their state.
The comparison baseline is `HEAD`, where Asset and CPU preparation completed
before the render thread started and the render thread stayed parked until the
main thread published commit or abort.

## Evidence inspected

- `engine/runtime/engine.h`
- `engine/runtime/engine.cpp`
- `engine/runtime/runtime_startup.h`
- `engine/runtime/runtime_startup.cpp`
- `engine/runtime/runtime_global_context.h`
- `engine/runtime/runtime_global_context.cpp`
- `engine/runtime/render/render_system.h`
- `engine/runtime/render/render_system.cpp`
- `engine/editor/editor.h`
- `engine/editor/editor.cpp`
- `engine/editor/ui/editor_ui.h`
- `engine/editor/ui/editor_ui.cpp`
- `engine/test/unit/runtime/runtime_startup_test.cpp`
- `engine/test/unit/render/render_system_test.cpp`
- `engine/test/unit/editor/editor_ui_lifecycle_test.cpp`
- Asset plans, roadmap, cross-stage spec/journal, Render lifecycle, Editor
  module document, validation matrix, project status, and the current Git diff

## Review round 1 — 2026-09-04

### F1 — close or render failure can destroy Runtime state while startup uses it

- Priority: P0
- Location: `engine/runtime/engine.cpp:231-285, 649-681`,
  `engine/runtime/runtime_global_context.cpp:259-294`
- Status: fixed

LO2 makes the render thread poll close events while the main thread is inside
the synchronous Asset load, CPU preparation, or Gameplay finalization. On
close, or on a loading-frame exception, the render thread sets the shutdown
flag and immediately calls `RuntimeContext::Clear()`. That function resets
`prepared_render_assets_`, `level_instance_`, `gameplay_world_`, input, Lua,
logging, and other services without waiting for the main thread to leave the
corresponding startup call.

The atomic shutdown flag is only observed between the synchronous stages, so
it is not a lifetime barrier. A close during `PrepareRenderAssets()` can race a
write to `prepared_render_assets_`; a close during `FinalizeGameStartup()` can
destroy objects while the main thread dereferences them. This is undefined
behavior and can crash or corrupt startup. The baseline did not pump events
until these main-thread operations were complete, so LO2 introduces the
overlap.

Cancellation/failure must first publish stop intent and wake waiters, then wait
for the main startup lane to acknowledge that it no longer accesses Runtime
state. Only then may teardown run in the required reverse ownership and thread
order. A deterministic close gate is needed to prove the barrier.

### F2 — failed startup leaves the Asset observation session non-terminal

- Priority: P1
- Location: `engine/runtime/engine.cpp:143-158, 214-230`
- Status: fixed

The startup Asset session is created before presentation starts but is sealed
only after `LoadStartupLevel()` returns successfully. Presentation failure,
thread creation failure, or an Asset-load exception takes the rollback path
without sealing it. The coordinator retains the session, so a rolled-back
startup snapshot can contain `asset.sealed == false` and
`asset.terminal == false`; its elapsed time also continues to advance after the
Runtime transaction is terminal.

Session sealing should be part of startup-scope cleanup and execute on every
success, failure, cancellation, and early-exception path. Tests should assert
that the nested Asset snapshot is terminal whenever the Runtime transaction is
terminal.

### F3 — terminal coordinator states can be rewritten by ordinary mutators

- Priority: P1
- Location: `engine/runtime/runtime_startup.cpp:49-119, 172-215`
- Status: fixed

Only `SetPhase()` and `SetReady()` consult `IsLegalTransition()`.
`SetProgress()`, `SetAssetSession()`, `Fail()`, and `Cancel()` mutate state
unconditionally. In particular, the render loop calls `Cancel()` when the user
closes the normal ready window, rewriting `Ready` to `Cancelled` even though
`Ready` is classified as terminal. Repeated failure/cancel calls can likewise
rewrite terminal phase and revision.

The transition table is internally inconsistent too: `Cancelled` is terminal
and `IsLegalTransition()` permits rollback only from `Failed`, while the public
`Rollback()` method directly performs `Cancelled -> RolledBack`. Enforce the
closed transition contract in every mutator and distinguish post-startup
application shutdown from startup cancellation. Add exhaustive transition and
terminal-idempotence tests.

### F4 — the rollback guard begins after fallible startup acquisitions

- Priority: P1
- Location: `engine/runtime/engine.cpp:136-187`
- Status: fixed

Editor attachment and Asset-session allocation happen before the scope guard,
and the guard itself is constructed only after `std::thread` creation. If
attachment/session setup or thread construction throws, no startup abort,
session seal, Editor detach, or coordinator rollback occurs. For example,
Editor attachment stores global borrowed pointers, then a failed session
allocation or thread creation leaves those bindings attached while the
coordinator remains `Failed` rather than completing rollback.

Establish transaction cleanup before the first fallible mutation, then make
each acquisition explicitly reversible. Add injected failures for every
pre-thread and thread-start boundary.

### F5 — the coordinator revision waiter cannot observe Asset-only progress

- Priority: P2
- Location: `engine/runtime/runtime_startup.cpp:138-169`
- Status: fixed

`GetSnapshot()` embeds a live Asset snapshot, but `WaitForRevision()` waits only
for `state_.revision`. Asset operations advance their own nested revision and
do not notify `changed_cv_`. During a long `LoadingAssets` phase, a caller that
waits on the returned Runtime revision cannot wake for any Asset progress and
will commonly sleep until Asset loading has already ended and the Runtime phase
changes.

Either make the outer revision/notification cover every observable snapshot
change, provide a waiter that also accepts the nested Asset revision, or remove
the misleading waiter and document polling as the sole contract. Cover the
chosen semantics with an Asset-only progress test.

### F6 — post-promotion loading frames bypass the fixed Editor pass contract

- Priority: P2
- Location: `engine/runtime/engine.cpp:596-645, 811-829`,
  `engine/editor/ui/editor_ui.cpp:279-292`,
  `engine/runtime/render/render_system.cpp:177-263`
- Status: fixed

After scene promotion succeeds, the local loop decision remains `Promote`, so
the same iteration runs `RenderLoadingTick()`. `RenderSystem::BeginFrame()` now
records the full deferred scene schedule, but `EditorUI::RenderLoading()` calls
the presentation renderer directly instead of entering
`ExecuteEditorCompositePass()`. `EndFrame()` therefore finalizes the fixed
schedule as if the terminal Editor pass was skipped even though ImGui was
recorded separately.

Keep presentation-only loading frames outside the scene schedule or route the
loading draw through the fixed terminal pass once scene capability exists.
Add an orchestration assertion for the promotion-to-commit interval.

## Reference comparison

- bgfx's macOS example requests exit, lets the renderer drain to `NoContext`,
  and joins the worker before completing teardown. The platform and ownership
  model differ, but the stop/quiesce/cleanup ordering applies directly to F1:
  [`entry_osx.mm`](https://github.com/bkaradzic/bgfx/blob/master/examples/common/entry/entry_osx.mm).
- Godot's Web entry point cancels its main loop, switches to an exit callback,
  and waits for shutdown completion before `Main::cleanup()`. Its web loop is
  not reusable here, but its explicit asynchronous shutdown acknowledgement
  supports the same lifetime barrier:
  [`web_main.cpp`](https://github.com/godotengine/godot/blob/master/platform/web/web_main.cpp).
- GLFW documents event processing as an event-loop responsibility and provides
  `glfwPostEmptyEvent` to wake a waiting main thread. KimPeanutEngine's existing
  render-thread-owned window loop is a Windows-first constraint, so the useful
  lesson is coordinated wakeup rather than copying GLFW's usual thread layout:
  [`input.md`](https://github.com/glfw/glfw/blob/master/docs/input.md).

No local reference study already resolves the new two-lane startup teardown
problem. The external patterns support an acknowledgement barrier but do not
determine which KimPeanutEngine owner should perform each partial teardown;
that must follow the project's existing Gameplay-before-Render and
render-thread GPU-affinity rules.

## Positive observations

- The capability split keeps one backend, `RenderSystem`, ImGui context, and
  `Editor` identity across presentation and scene readiness.
- `RenderSystem::PromoteToScene()` rolls a failed scene acquisition back to
  `PresentationReady`, which is the right retry/failure boundary.
- Startup snapshots are copied values and coordinator locking does not call
  Asset, Render, Graphics, Gameplay, or Editor.
- The main-thread Asset/CPU work remains synchronous, so the implementation
  does not misrepresent `LoadAsync` as parallel startup scheduling.
- The normal ready-mode game/render handshake is retained after commit.

## Review round 2 — 2026-09-04

The implementation now applies the following corrections:

- F1: `StartupAccessBarrier` covers the main startup access lane. Render-side
  cancellation publishes stop intent and waits for that lane before clearing
  Runtime state; the startup rollback guard ends the lane before joining the
  render thread.
- F2: startup-scope cleanup seals the Asset observation session on success,
  failure, cancellation, and early exceptions. Coordinator failure/cancel
  paths also seal a retained session without holding the coordinator mutex.
- F3: ordinary coordinator mutators are terminal-idempotent, and rollback is
  accepted only from `Failed` or `Cancelled`. Normal ready-mode window close no
  longer becomes startup cancellation.
- F4: the rollback guard is established before Editor attachment, session
  allocation, and render-thread creation, and reverses those acquisitions.
- F5: `WaitForRevision()` accepts the nested Asset revision and observes
  Asset-only progress using a bounded poll because the Asset session API has no
  shared condition variable.
- F6: loading frames use `ExecuteEditorCompositePass()` whenever the scene
  renderer has been promoted, preserving the fixed terminal Editor pass.

## Validation

Current-tree review validation on 2026-09-04:

- `cmake --build build --config Debug --parallel 1` — passed.
- `ctest --test-dir build -C Debug --output-on-failure -R
  "(RenderSystemLifecycleTest|RuntimeStartupTest|RuntimeFramePolicyTest|RuntimeStartupCoordinatorTest|EditorUILifecycleTest)"`
  — passed, 23/23 tests.
- `ctest --test-dir build -C Debug --output-on-failure` — passed, 281/281 tests.
- `git diff --check` — passed; Git emitted only sandboxed global-ignore and
  working-copy line-ending warnings.

The new focused tests cover terminal-state idempotence, Asset-only revision
wakes, and the startup access barrier. Existing lifecycle tests cover Render
capability promotion and Editor partial initialization. The build and tests do
not constitute the LO2 visual acceptance gate.

The LO2 acceptance gate remains incomplete: no Vulkan/OpenGL loading-frame
count, close/failure matrix, first-ready-frame proof, or loading/success/failure
visual evidence is recorded. Those are LO3/visual follow-up items, not open
findings in this risk pass.
