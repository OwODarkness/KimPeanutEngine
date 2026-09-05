# LO2 — Staged Runtime Startup and Editor Promotion

- Status: complete
- Parent roadmap: [Asset Loading Progress TODO](../TODO.md)
- Depends on: [LO1 — Asset Load Observation](LO1.md)
- Cross-stage spec: [Asset Loading Progress](../../../.spec/specs/asset-loading-progress.md)
- Review: [LO2 review](../.review/LO2.md)

## Objective

Replace the current all-or-nothing initialization sequence with a transactional
startup state machine that can present frames and poll window events while the
startup level, CPU artifacts, scene GPU resources, and Gameplay instance become
ready. Preserve one Editor instance throughout startup, but expose only the
capabilities valid in each phase so future scene editing cannot accidentally
run on the render thread or before the Gameplay world exists.

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
  -> ActivatingEditorWorkspace
  -> Ready

Any non-terminal state -> Failed or Cancelled -> RolledBack
```

This is capability promotion, not construction of a temporary engine followed
by a second full engine. Window, graphics backend, ImGui context, and `Editor`
identity survive the transition. Scene-only objects are added after their
dependencies become valid.

The corresponding immutable `StartupSnapshot` contains transaction
identity, revision, state/phase, current display label, exact work counts when
known, optional presentation fraction, nested Asset snapshot revision/data, and
terminal diagnostic. Runtime publishes it; Editor never derives global
readiness by inspecting subsystems.

## Startup sequence

1. Parse launch/bootstrap selection and choose the graphics API without loading
   the level.
2. Attach the Editor to loading-safe Runtime services on the game thread. This
   supplies the startup snapshot source and services such as logging, but no
   Gameplay world, scene target, camera, selection model, or mutable Asset
   access.
3. Start the render thread and initialize the existing WindowSystem, graphics
   backend, frame/presentation resources, and Editor presentation bridge in a
   minimal presentation state. Create the ImGui context and loading UI once.
4. Signal `PresentationReady`. The main/game thread performs the existing
   synchronous Asset load and CPU `RenderAssetPreparer` transaction while the
   render thread independently polls events and presents loading frames.
5. Publish the immutable prepared catalog to the render thread. Promote the
   same backend and `RenderSystem` from presentation-ready to scene-ready by
   creating resolvers, frame/scene resources, and `DeferredRenderer` state.
6. Finalize Gameplay/level startup on the game thread only after Render source
   sinks are valid. Preserve the current preferred-camera and commit-or-abort
   rules.
7. Bind the Editor workspace adapters on the game thread after Gameplay and
   scene capabilities exist. This is the future home for selection, edit-mode
   commands, gizmo requests, play/edit state, and viewport-world coordination.
8. Publish `ActivatingEditorWorkspace`. At the next render-frame boundary,
   build the normal Editor tool tree exactly once, retire the loading tree, and
   acknowledge promotion without drawing a premature normal frame.
9. After the acknowledgement, publish `Ready` and enter the existing normal
   game/render frame handshake. The next presentation is therefore the first
   normal frame and sees the committed world and catalog.

The main-thread Asset work may remain synchronous initially. Responsiveness
comes from the independently pumping render thread; LO2 does not require
pretending that `LoadAsync` makes the serialized loader pipeline parallel.

## Thread and tick contract

The plan deliberately does not add two ambiguous `Editor::Tick` overloads.
Loading versus ready is a presentation mode; editor-world behavior is a thread
ownership distinction.

| Entry point | Thread | Available during loading | Responsibility |
|---|---|---:|---|
| `Editor::Attach(...)` | game/main | yes | Bind loading-safe value sources and non-GPU services. |
| `Editor::InitializePresentation(...)` | render | yes | Create ImGui/WSI/renderer once and build the loading tree. |
| `Editor::TickPresentation()` | render | yes | Poll the copied startup snapshot, draw the current UI mode, and record ImGui. |
| `Editor::ActivateWorkspace(...)` | game/main | no | Bind scene/gameplay editing adapters after successful Runtime composition. |
| `Editor::TickWorkspace(delta)` | game/main | no | Run future editor domain behavior and submit commands through Runtime-owned seams. |
| `Editor::PromotePresentationToWorkspace()` | render | no | At a frame boundary, replace the loading tree with scene-aware panels exactly once. |
| `Editor::ShutdownPresentation()` | render | yes | Tear down ready or partially initialized ImGui state. |
| `Editor::Detach()` | game/main after join | yes | Release non-owning Runtime bindings and editor domain state. |

Names are proposed, not a requirement to preserve exactly. The required
contract is explicit thread affinity and capability checks. In particular,
`TickPresentation()` must never mutate Gameplay directly. UI actions enqueue or
publish typed commands that `TickWorkspace()` applies on the game thread.

During loading there is no game/render frame-production dependency: the render
thread runs a self-paced loading presentation loop. After `Ready`, Runtime
switches exactly once to the existing produced-frame handshake, and only then
calls `TickWorkspace(delta)` as part of the game-thread frame.

## Ownership

- `Engine` owns the startup coordinator, state transitions, worker/render
  synchronization, and the single `Editor` lifetime.
- `RuntimeContext` owns Runtime systems and their partial/full capability state.
- `RenderSystem` owns presentation-ready and scene-ready resources; Graphics
  retains GPU object and synchronization ownership.
- `Editor` owns editor domain state and mode selection, but borrows narrow
  Runtime interfaces. It does not own the startup transaction or Gameplay
  world.
- `EditorUI` owns ImGui context/backends and mutually exclusive loading and
  workspace component trees on the render thread.

The existing RuntimeLib/EditorLib cycle must not grow. New startup coordination
types live in Runtime and contain no Editor types. Calls into `Editor` remain at
the existing Engine composition edge until that cycle is separately removed.

## Proposed Runtime contracts

Introduce a small Runtime-owned coordinator (name illustrative):

```cpp
enum class StartupPhase : uint8_t {
    Cold,
    PresentationStarting,
    PresentationReady,
    LoadingAssets,
    PreparingCpuArtifacts,
    PromotingSceneRenderer,
    InstantiatingLevel,
    ActivatingEditorWorkspace,
    Ready,
    Failed,
    Cancelled,
    RolledBack,
};

struct StartupSnapshot {
    uint64_t transaction_id;
    uint64_t revision;
    StartupPhase phase;
    StartupProgress progress;
    std::optional<asset::AssetLoadSnapshot> asset;
    std::string display_label;
    std::string diagnostic;
};
```

Snapshot publication is value-based. One mutex may protect coordinator
transition state, but it must not be held while calling Asset, Render, Graphics,
Gameplay, or Editor. Wait predicates observe terminal/capability facts rather
than Editor-specific events.

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
- Failure after Editor presentation initialization keeps the loading shell
  alive long enough to render the copied diagnostic unless the window is
  already closing. It never activates workspace behavior.
- If workspace adapters were bound but presentation promotion failed, detach
  those adapters before destroying Gameplay/scene state; then tear down ImGui,
  backend, and window on their owning thread.

## Implementation stages

1. **LO2.1 — coordinator characterization:** extract and test the pure Runtime
   state machine, immutable snapshot publication, legal transitions, and
   first-failure preservation without changing live startup order.
2. **LO2.2 — presentation capability:** split Window/Render initialization into
   presentation-ready and scene-ready acquisition with partial rollback tests.
3. **LO2.3 — Editor shell lifecycle:** replace the coarse current
   `Initialize`/`InitEditorUI` contract with attachment and presentation
   initialization while keeping current ready-mode behavior unchanged.
4. **LO2.4 — live loading loop:** start presentation first, run synchronous
   Asset/CPU preparation on the game thread, and prove event/frame progress
   while deterministic gates are held.
5. **LO2.5 — scene and workspace promotion:** promote Render, instantiate the
   level, bind game-thread Editor workspace adapters, replace the UI tree at a
   render-frame boundary, and switch once to the normal frame handshake.
6. **LO2.6 — hardening:** measure scene promotion, budget only demonstrated
   stalls, cover failure/close at every boundary, and validate both backends.

Each stage must leave a buildable lifecycle. Do not land a call site that can
invoke scene APIs while only presentation-ready.

## Acceptance criteria

- [ ] Presentation reaches a frame-capable state without a prepared catalog,
  ready Gameplay world, instantiated level, or scene render target. Dormant
  composition objects may exist but expose no scene-ready capability.
- [ ] A deterministic Asset/CPU gate allows at least two event polls and two
  presented loading frames on both backends.
- [ ] Editor presentation is initialized once and survives loading-to-ready;
  the ImGui context and graphics backend are not recreated.
- [ ] Loading presentation never reads scene-only services.
- [ ] Editor workspace adapters and `TickWorkspace` are game-thread-only and
  become callable only after Runtime composition succeeds.
- [ ] The loading tree is replaced by the workspace tree exactly once at a
  render-frame boundary.
- [ ] The first ready frame observes the committed prepared catalog, level, and
  preferred camera.
- [ ] Failure or close at every acquisition/promotion step wakes all waiters and
  releases state in reverse order on its owning thread.
- [ ] The Runtime snapshot remains authoritative and retains the first terminal
  diagnostic without calling Editor under the coordinator lock.

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
