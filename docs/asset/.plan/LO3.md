# LO3 — Editor Loading Presentation

- Status: complete
- Parent roadmap: [Asset Loading Progress TODO](../TODO.md)
- Depends on: [LO2 — Staged Runtime Startup and Editor Promotion](LO2.md)
- Cross-stage spec: [Asset Loading Progress](../../../.spec/specs/asset-loading-progress.md)

## Objective

Show a loading screen with a truthful progress bar first, then replace it once
with the existing main Editor UI after Runtime has committed the scene-ready
workspace. The loading screen consumes copied Runtime transaction snapshots and
remains safe before scene rendering exists.

The user-visible contract is deliberately simple:

```text
launch -> loading screen + progress bar -> main Editor UI
```

There is no blank interval, mixed loading/main frame, or second ImGui context.

## Consumer boundary

Expose a narrow read-only Runtime interface equivalent to:

```cpp
class ILoadProgressSnapshotSource {
public:
    virtual ~ILoadProgressSnapshotSource() = default;
    virtual runtime::StartupSnapshot GetSnapshot() const = 0;
};
```

Runtime owns the implementation and lifetime. `EditorContext` receives a
non-owning pointer whose validity is bounded by the existing Engine/Editor
lifecycle. Editor polls once per rendered loading frame. The interface returns
a value snapshot; it does not return references into mutable storage and does
not let Editor acknowledge, cancel, retry, or mutate the transaction in LO3.

## UI modes

`EditorUI` gains explicit modes:

- **Loading:** only loading-safe components exist. No viewport texture,
  RenderSystem scene target, material, Gameplay, or capture service is read.
- **Workspace:** the normal menu, console, viewport, log, and profile component
  tree exists only after Runtime has committed its scene-ready dependencies and
  requested Editor workspace promotion.
- **Failed:** retain event pumping and show the terminal stage and diagnostic;
  production policy may close after acknowledgement in a later design.

The transition is one-way for startup. Live level transitions may reuse the
snapshot/view model later, but are outside LO3.

These are `EditorUI` presentation modes, not two complete Editor instances and
not two domain update loops. The ImGui context, WSI, API renderer, and
`Editor::TickPresentation()` remain continuous across the transition.

## Exact presentation sequence

1. `InitializePresentation` creates the existing ImGui context, WSI, and API
   renderer, then calls `BuildLoadingTree`. It does not build the main UI.
2. Every loading-frame `TickPresentation` reads one copied Runtime snapshot,
   derives a loading view model, and draws the loading tree and progress bar.
3. When Runtime reaches `ActivatingEditorWorkspace`, the render thread receives
   an exactly-once promotion request. Scene-ready adapters have already been
   bound on the game thread.
4. At the start of a render-frame boundary, construct the workspace tree in
   temporary ownership. Only after construction succeeds, swap it into the
   active tree and destroy the loading tree.
5. Acknowledge promotion without drawing a mixed frame. Runtime publishes
   `Ready` and switches to the normal frame handshake.
6. The next `TickPresentation` draws the existing main Editor UI. No later
   startup snapshot can switch the UI back to Loading.

If workspace-tree construction fails, retain the loading tree, switch it to the
Failed view, and return a diagnostic to Runtime. Never destroy the only
renderable UI before its replacement is valid.

The currently staged LO2 code has separate `Editor::TickPresentation()`
(loading) and `Editor::Tick()` (workspace) calls. LO3 consolidates these at the
Editor boundary: both Runtime frame loops call `TickPresentation()`, which
dispatches by `EditorUI` mode. The Runtime loading and normal frame loops may
remain separate because their Render capabilities and synchronization differ;
the Editor does not need two render-thread UI tick APIs.

## Loading component

Add a focused `EditorLoadingComponent` or equivalent full-viewport panel. It
renders:

- the Runtime stage label;
- the current Asset/display item using a safe relative/display path;
- exact `completed / known` counts when meaningful;
- a determinate progress bar only when Runtime marks the value determinate;
- an indeterminate activity indicator otherwise;
- a concise terminal diagnostic and failing stage.

The V1 visual needs only an engine/loading title, stage text, current item text,
and one prominent progress bar. Do not delay LO3 for tips, animation assets,
branding, cancel/retry controls, or a general loading-screen framework.

Extract snapshot-to-view-model logic from ImGui calls so formatting and state
selection can be unit-tested. The component owns only presentation-local state,
such as the last displayed revision and monotonic fraction within the current
stage.

## Presentation rules

- Never display `100%` before Runtime state is `Ready`.
- The last loading frame may remain below `100%`; the transition to the main UI
  is the completion signal. Do not fake a `100%` frame or add a minimum delay.
- Never turn an unknown total into `0%`; show indeterminate activity instead.
- The bar consumes Runtime's optional normalized presentation fraction; Editor
  may clamp and smooth it for display but never invent stage weights.
- When that fraction is absent, draw the same bar control in an indeterminate
  animated state and omit a numeric percentage.
- Exact counts remain visible even when the smoothed/clamped bar is monotonic.
- A stage change may reset stage-local detail, while the overall presentation
  remains nondecreasing according to Runtime's bounded stage policy.
- Diagnostics are copied text. Editor does not retain pointers to Asset or
  exception objects.
- Rendering the screen must not hold Asset, Runtime transaction, or Render
  lifecycle locks.

## Integration boundary

- `Editor::Attach` binds the snapshot source before scene readiness on the game
  thread and performs no GPU work.
- `Editor::InitializePresentation` runs on the render thread after minimal
  presentation is available, creates ImGui once, and builds the loading tree.
- `Editor::TickPresentation` runs for both loading and ready modes. It draws
  whichever component tree is active; it does not become an editor-world tick.
- `Editor::ActivateWorkspace` runs on the game thread after scene-ready commit
  and binds narrow editing adapters. Future selection, gizmo, play/edit, and
  viewport-world behavior runs through `Editor::TickWorkspace` on that thread.
- `Editor::PromotePresentationToWorkspace` runs at the next render-frame
  boundary, transactionally creates the normal tool tree exactly once, and
  acknowledges success to Runtime. Scene-dependent components must not merely
  null-check their way through partial startup; they do not exist before
  promotion.
- The existing API-specific ImGui renderers and typed presentation bridge are
  reused. No Asset or Runtime type enters the OpenGL/Vulkan renderer classes.
- UI interactions that change scene/gameplay state publish typed requests to a
  Runtime/editor command seam. The render thread never writes directly through
  current raw scene pointers; existing direct bindings must be audited as each
  scene-aware tool is restored.

## Editor lifecycle states

Use explicit state checks equivalent to:

```text
Detached
  -> Attached
  -> PresentationReady(Loading)
  -> WorkspaceBound
  -> PresentationReady(Workspace)
  -> PresentationClosed
  -> Detached
```

Failure may move `Attached`, `PresentationReady(Loading)`, or `WorkspaceBound`
to teardown without entering Workspace presentation. Repeated activation,
promotion, close, and detach are rejected or idempotent according to their
cleanup role; no boolean pair may represent an impossible mixed state.

## Implementation order

1. **LO3.1 — view model:** implement and unit-test pure
   `runtime::StartupSnapshot` to loading-text/bar-state conversion.
2. **LO3.2 — loading-first initialization:** split current `EditorUI::Initialize`
   so presentation services are created once and only `BuildLoadingTree` runs
   initially. Consolidate current loading/workspace Editor UI tick entry points
   behind mode-aware `TickPresentation()`.
3. **LO3.3 — progress rendering:** add the full-viewport loading component and
   draw determinate or indeterminate progress on every loading frame.
4. **LO3.4 — main-UI promotion:** move construction of the existing main tool
   tree behind transactional, exactly-once workspace promotion and add the
   acknowledgement to LO2's coordinator.
5. **LO3.5 — failure and visual evidence:** preserve the loading tree for
   diagnostics, cover close/failure, and capture loading plus first-main-UI
   frames on Vulkan and OpenGL.

LO3 is complete only when the real executable visibly presents the loading
screen before the current main UI. A standalone component test is necessary but
not sufficient.

Current implementation lands the snapshot view model, loading component,
loading-first tree, exactly-once workspace promotion, and a runtime
`capture.screenshot` `engine_window` view for final client-area captures. The
real executable loading-first visual gate still needs a deterministic startup
hold and inspected Vulkan/OpenGL loading captures.

## Validation

- Pure view-model tests cover queued/running, unknown totals, expanding totals,
  cache hits, monotonic display, `Ready`, and failure diagnostics.
- Editor lifecycle tests prove the loading tree contains no scene component and
  the workspace tree is built exactly once after scene-ready commit.
- A deterministic gate proves the rendered order is one or more Loading frames
  followed by exactly one first Workspace frame, with no blank or mixed frame.
- Thread-affinity tests/assertions prove presentation calls are render-thread
  only and workspace behavior is game-thread only.
- A deterministic injected startup gate provides enough frames for visual
  validation without adding a production sleep or committing temporary assets.
- Vulkan and OpenGL evidence includes an active-loading frame, the successful
  first ready frame, and a failure-state frame or deterministic UI render test.
- Closing the window during loading exits without touching scene-only state.

## Non-goals

- Shipping-game loading-screen theming or a public UI framework.
- Cancel/retry buttons, background tips, animation asset streaming, or live
  level transitions.
- Editor ownership of startup scheduling, Asset dependency aggregation, or
  Render promotion.
- Implementing selection, gizmos, play/edit state, or other future
  `TickWorkspace` behavior. LO3 preserves their game-thread boundary only.
