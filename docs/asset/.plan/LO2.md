# LO2 — Staged Runtime Startup Loop

- Status: proposed
- Parent roadmap: [Asset Loading Progress TODO](../TODO.md#lo2--staged-runtime-startup-loop)
- Depends on: [LO1 — Asset Load Observation](LO1.md)
- Cross-stage spec: [Asset Loading Progress](../../../.spec/specs/asset-loading-progress.md)

## Objective

Replace the current all-or-nothing initialization sequence with a transactional
startup state machine that can present frames and poll window events while the
startup level, CPU artifacts, scene GPU resources, and Gameplay instance become
ready.

## Current state and concrete problem

`Engine::Initialize` synchronously calls `LoadStartupLevel` and
`RuntimeContext::PrepareRenderAssets` before `Editor::Initialize` and before the
render thread creates the window. `RuntimeContext::Initialize` then initializes
the window, input, Lua, and a `RenderSystem` that requires a complete prepared
catalog. The render thread creates ImGui only after Runtime startup commits.

Consequently, an Asset snapshot alone cannot be displayed. There is no window,
presentation bridge, or Editor UI during the expensive Asset and CPU
preparation work.

## Chosen lifecycle

Use one Runtime-owned transaction with explicit states equivalent to:

```text
Cold
  -> PresentationStarting
  -> PresentationReady
  -> LoadingAssets
  -> PreparingCpuArtifacts
  -> PromotingSceneRenderer
  -> InstantiatingLevel
  -> Ready

Any non-terminal state -> Failed or Cancelled -> RolledBack
```

The corresponding immutable `LoadTransactionSnapshot` contains transaction
identity, revision, state/phase, current display label, exact work counts when
known, optional presentation fraction, nested Asset snapshot revision/data, and
terminal diagnostic. Runtime publishes it; Editor never derives global
readiness by inspecting subsystems.

## Startup sequence

1. Parse launch/bootstrap selection and choose the graphics API without loading
   the level.
2. Start the render thread and initialize the existing WindowSystem, graphics
   backend, frame/presentation resources, and Editor presentation bridge in a
   minimal presentation state. Initialize only the loading UI.
3. Signal `PresentationReady`. The main/game thread performs the existing
   synchronous Asset load and CPU `RenderAssetPreparer` transaction while the
   render thread independently polls events and presents loading frames.
4. Publish the immutable prepared catalog to the render thread. Promote the
   same backend and `RenderSystem` from presentation-ready to scene-ready by
   creating resolvers, frame/scene resources, and `DeferredRenderer` state.
5. Finalize Gameplay/level startup on the game thread only after Render source
   sinks are valid. Preserve the current preferred-camera and commit-or-abort
   rules.
6. Publish `Ready`, replace the loading-only Editor tree with the normal tools,
   and enter the existing game/render frame handshake.

The main-thread Asset work may remain synchronous initially. Responsiveness
comes from the independently pumping render thread; LO2 does not require
pretending that `LoadAsync` makes the serialized loader pipeline parallel.

## Render boundary

Split initialization by capability, not by creating a parallel renderer:

- **Presentation-ready:** window/context, backend presentation resources,
  resize/event handling, Editor presentation bridge, and the minimal frame
  bracket necessary to clear, draw ImGui, submit, and present.
- **Scene-ready:** prepared catalog, resource resolver, material system, frame
  scene data, deferred renderer, capture service, and source registries.

`RenderSystemLifecycleState` must represent this distinction. Scene methods and
scene-dependent Editor panels reject or remain disabled before scene readiness.
Rollback releases scene-ready objects before presentation/backend/window
objects, preserving GPU-safe destruction.

If scene promotion performs long blocking GPU work on the render thread,
instrument it. Work that violates the loading-frame responsiveness bound must
be converted to incremental/budgeted promotion steps at frame boundaries. Do
not create a second graphics context or upload from an unauthorized thread to
hide the stall.

## Progress policy

- Runtime stage is authoritative; Asset status is nested evidence.
- Exact counts and current labels are always preferred over a fabricated
  percentage.
- A determinate presentation fraction may allocate bounded ranges to known
  stages and clamp within each stage, but it must stay below completion until
  `Ready` is published.
- Failure is terminal and retains the first actionable diagnostic plus the
  failing subsystem/stage.
- Cache-hit startup remains observable and may advance quickly; tests must not
  depend on a minimum display duration in production.

## Failure and shutdown

- Window/backend failure before presentation readiness returns the existing
  initialization error without claiming that a screen was shown.
- Asset/preparation/promotion/finalization failure publishes `Failed`, wakes all
  waiters, stops further promotion, and rolls back in reverse ownership order.
- Window-close during loading requests cancellation/shutdown and wakes both
  startup and frame waits.
- No thread entry point lets an exception escape. `Engine::Clear` remains
  terminal and idempotent with respect to partial startup.

## Implementation stages

1. Extract and test a pure Runtime startup state machine and immutable snapshot
   publication without changing the live lifecycle.
2. Split Runtime/Render initialization into presentation-ready and scene-ready
   capabilities with rollback tests.
3. Reorder live startup so the presentation loop runs during existing Asset and
   CPU preparation.
4. Measure scene promotion and make expensive work incremental only where the
   responsiveness evidence requires it.
5. Integrate level finalization, normal frame-loop handoff, cancellation, and
   failure diagnostics.

## Validation

- Unit-test legal transitions, exactly-once commit/abort, first-failure
  preservation, waiter wakeups, and immutable snapshots.
- Inject gates into Asset preparation and scene promotion; prove multiple event
  polls/presented loading frames occur while each gate is held.
- Test close/cancel and failure at every acquisition stage; verify reverse
  teardown and no scene API use before readiness.
- Run full Debug build and CTest because Engine, Runtime, Render, Graphics, and
  Editor lifecycle boundaries change.
- Run Vulkan and OpenGL smoke and inspect loading plus first-ready captures.
