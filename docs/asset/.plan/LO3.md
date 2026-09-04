# LO3 — Editor Loading Presentation

- Status: proposed
- Parent roadmap: [Asset Loading Progress TODO](../TODO.md#lo3--editor-loading-presentation)
- Depends on: [LO2 — Staged Runtime Startup Loop](LO2.md)
- Cross-stage spec: [Asset Loading Progress](../../../.spec/specs/asset-loading-progress.md)

## Objective

Add an Editor loading presentation that consumes one copied Runtime transaction
snapshot and remains safe before scene rendering exists. It must communicate
real state, remain responsive, and transition once to the normal Editor.

## Consumer boundary

Expose a narrow read-only Runtime interface equivalent to:

```cpp
class ILoadProgressSnapshotSource {
public:
    virtual ~ILoadProgressSnapshotSource() = default;
    virtual LoadTransactionSnapshot GetSnapshot() const = 0;
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
- **Ready:** the normal menu, console, viewport, log, and profile component tree
  is built after Runtime publishes `Ready` and Render is scene-ready.
- **Failed:** retain event pumping and show the terminal stage and diagnostic;
  production policy may close after acknowledgement in a later design.

The transition is one-way for startup. Live level transitions may reuse the
snapshot/view model later, but are outside LO3.

## Loading component

Add a focused `EditorLoadingComponent` or equivalent full-viewport panel. It
renders:

- the Runtime stage label;
- the current Asset/display item using a safe relative/display path;
- exact `completed / known` counts when meaningful;
- a determinate progress bar only when Runtime marks the value determinate;
- an indeterminate activity indicator otherwise;
- a concise terminal diagnostic and failing stage.

Extract snapshot-to-view-model logic from ImGui calls so formatting and state
selection can be unit-tested. The component owns only presentation-local state,
such as the last displayed revision and monotonic fraction within the current
stage.

## Presentation rules

- Never display `100%` before Runtime state is `Ready`.
- Never turn an unknown total into `0%`; show indeterminate activity instead.
- Exact counts remain visible even when the smoothed/clamped bar is monotonic.
- A stage change may reset stage-local detail, while the overall presentation
  remains nondecreasing according to Runtime's bounded stage policy.
- Diagnostics are copied text. Editor does not retain pointers to Asset or
  exception objects.
- Rendering the screen must not hold Asset, Runtime transaction, or Render
  lifecycle locks.

## Integration boundary

- `Editor::Initialize` may bind the snapshot source before scene readiness, but
  it performs no GPU work.
- `Editor::InitEditorUI` runs on the render thread after minimal presentation is
  available and builds the loading tree only.
- The normal tool tree is created after scene-ready commit. Scene-dependent
  components must not merely null-check their way through partial startup; they
  should not exist yet.
- The existing API-specific ImGui renderers and typed presentation bridge are
  reused. No Asset or Runtime type enters the OpenGL/Vulkan renderer classes.

## Validation

- Pure view-model tests cover queued/running, unknown totals, expanding totals,
  cache hits, monotonic display, `Ready`, and failure diagnostics.
- Editor lifecycle tests prove the loading tree contains no scene component and
  the ready tree is built exactly once after commit.
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
