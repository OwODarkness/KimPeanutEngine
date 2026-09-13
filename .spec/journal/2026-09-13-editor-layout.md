# Editor Layout Model — ED2

- Date: 2026-09-13
- Plan: [ED2](../../docs/editor/.plan/ED2.md)
- Roadmap: [Editor TODO](../../docs/editor/TODO.md)
- Builds on: [ED1 journal](2026-09-13-editor-tool-row.md)
- Scope: named regions in a splitter tree, reflow, draggable seams, and persisted layout

## Result

Panel geometry now comes from one layout model. Every workspace panel declares a slot; the
model resolves the slots to rectangles once per frame; seams between regions are draggable;
and the result persists to `save/editor_layout.json`.

Two pre-existing bugs were fixed as a consequence rather than as separate work, because
they were the same bug: three independent hardcoded ratios described the bottom band and
disagreed with each other.

- The GPU Profiler was configured `y 0.70` with `height 0.34` — its bottom at **1.04×** the
  work area. ImGui does not clamp a position set through the API, so it drew outside the
  work area and over the Profile bar's Memory metric.
- The tool row's bottom was `0.96×` the work area while the bar's top was `H − 43`. Below
  about 1075 px tall that overlapped the bar; above it, up to 1440p, it left a **13 px gap**
  of bare background.

Neither was catchable before, because no code knew the set of regions, so nothing could
assert they tile. That assertion is now a test, and it is the one that fails if either bug
is reintroduced.

### Semantics settled by this stage

- **Fractions are parent-relative**, which is what makes reflow automatic. A split's
  children derive from its own rect, so resizing the window or moving any seam updates
  everything below it with no bookkeeping.
- **The tool row's bottom is the workspace bottom**, so it meets the status bar by
  construction. The old layout described the same edge twice with two different numbers.
- **Two split modes.** Proportional for everything the user resizes; a fixed-pixel split
  for the status bar, whose height is font- and theme-derived rather than a ratio. The
  editor pushes the measured height each frame; the model carries a constant only so a
  headless resolve produces the same layout the app does.
- **One clamp, used by both the resolver and the drag.** A stored fraction can describe a
  child below its minimum without any drag happening — resizing the window changes the
  extent the fraction is measured against — so resolve-time clamping is the only thing that
  can guarantee the invariant. Before this, only the drag path clamped.
- **A too-small parent shares the deficit** between the minima proportionally rather than
  starving one child, so tiling stays exact and no extent goes negative.
- **Seam geometry comes from the resolved children, not from the fraction**, so a clamped
  split puts its handle on the visible edge.
- **`EditorRect` moved** out of `editor_tool_row_model.h` into `editor_layout_rect.h`, so
  the layout model does not have to include the tool-row header. There is still exactly one
  rectangle type; the tool-row model's code and tests were unchanged.
- **Layout-placed windows lose the padlock**, which would otherwise offer "unlock to move
  freely" for a window whose rect the layout owns. The padlock, `locked_`, and
  `EditorWindowConfig::locked` remain for the two windows that place themselves — the
  Live2D viewer's log panel and the loading-tree Startup Profiler — so nothing outside
  `engine/editor` changed. `SlotConfig(slot)` carries no ratio at all, so a slotted panel
  has no dead geometry to misread as live.
- **`NoSavedSettings` on slotted windows**, which removes workspace geometry from
  `imgui.ini` entirely and stops a rect re-pushed every frame from being written on every
  frame of a drag.
- **Splitters are foreground draw-list strips, not windows.** A host window would have
  broken the mouse wheel for every panel (ImGui routes it to the hovered window) and would
  have been permanently covered by whichever panel was clicked last. Precedence against the
  panel beneath is resolved by requiring no item hovered or active before a drag starts, so
  a tab strip or an input box wins over the seam and blank background loses to it.
- **The drag base is the drag-start fraction**, applied against total displacement, so
  dragging past a minimum and back tracks the mouse instead of latching at the clamp. A
  test pins this specifically, and it fails against the naive accumulate-into-the-stored-
  value implementation.
- **Persistence is keyed by splitter ID, never index**, so ED3 inserting or removing a
  split cannot reattach a saved size to the wrong seam. It is its own file under `save/`
  rather than `config/settings.json`, which is checked in and read by two independent
  parsers — a rewrite there would clobber live2d's `window_background_color`.
- **`width_`/`height_`/`pos_x`/`pos_y` were deleted** from `EditorWindowComponent`. They had
  zero readers repo-wide and were stale at zero for every panel except the log, whose window
  no longer exists. This also retires the ED1 follow-up that limited which panels the tool
  row could host.

## Files

- `engine/editor/ui/component/editor_layout_rect.h` (new)
- `engine/editor/ui/component/editor_layout_model.h/.cpp` (new)
- `engine/editor/ui/component/editor_splitter_handles.h/.cpp` (new)
- `engine/editor/settings/editor_layout_settings.h/.cpp` (new)
- `engine/editor/ui/component/editor_ui_component.h` — the two delivery virtuals
- `engine/editor/ui/component/editor_window_component.h/.cpp` — `slot`, layout rect,
  chrome split, dead outputs removed
- `engine/editor/ui/component/editor_tool_row_model.h` — include move
- `engine/editor/profile/editor_profile_bar.h/.cpp` — slot, measured height
- five panel constructors, `engine/editor/ui/editor_ui.h/.cpp`
- `engine/runtime/core/config/path.h` — save-directory and layout-path getters
- `engine/editor/ui/component/CMakeLists.txt`, `engine/editor/settings/CMakeLists.txt`
- `engine/test/unit/editor/editor_layout_model_test.cpp` (new), `CMakeLists.txt`
- `docs/editor/.plan/ED2.md`, `docs/editor/TODO.md`, `docs/editor/editor_module.md`,
  `docs/status.md`

## Validation

- `cmake --build build --config Debug` — passed, no warnings.
- `EditorLayoutModelUnitTest` — **31/31 passed**. The load-bearing case is the tiling
  assertion swept over six work-area sizes: no region escapes the work area, no two
  overlap, and their areas sum to the work area exactly.
- `ctest --test-dir build -C Debug` — **654/655 passed**. The single failure is the
  pre-existing `LevelLoaderTest.LoadsCheckedInGameplayLevelFixtures` (checked-in
  `pbr_showcase.level` references `model/rock1-bl/rock2`, absent from the repository
  archive) and is unrelated.
- Editor smoke, both backends, via `capture.screenshot` with `view: engine_window`:
  - `save/screenshots/validation/ED2-vulkan-default-layout.png`
  - `save/screenshots/validation/ED2-opengl-default-layout.png`
  - `save/screenshots/validation/ED2-vulkan-no-ini.png`
  - `save/screenshots/validation/ED2-vulkan-saved-layout.png`

  The defaults show the Performance Profiler flush at the bottom instead of overflowing,
  the Tools row running down to the status bar with no gap, and no padlock on any region
  panel. No run logged an assert or a layout diagnostic.
- `git diff --check` — clean.

### What the smoke proved that ED1's could not

ED1's interactive behaviour went unverified because the command transport cannot inject
input. **ED2's resize path is reachable without it**, by writing a layout file and
relaunching:

- with `imgui.ini` **deleted**, the workspace rendered pixel-identically, and the
  regenerated `imgui.ini` contained only ImGui's own default window and the unslotted
  Startup Profiler — no workspace panel. The layout no longer depends on `imgui.ini` at all.
- with `save/editor_layout.json` setting `tool_row: 0.45`, `right_column: 0.62`, and
  `camera: 0.2`, the relaunch showed a 55%-tall tool row, a 38%-wide right column, and a
  short Camera Settings pane, still tiling with no gaps.

### Not run

Dragging a seam with the mouse, and the hover resize cursor. These need a hand on the
mouse; the arithmetic behind them is unit-tested, and the applied result is verified through
the persisted-layout capture above.

## Correction (2026-09-13, after the captures above)

**Reported:** closing the Viewport or the Debug Viewer dismissed it permanently — it never
came back.

**Cause:** a defect ED1 had only half-fixed, and which ED1's own journal and completion
report overstated as fixed.

`EditorWindowComponent::Render()` latches a close into `is_open_` when no visibility is
bound:

```cpp
if (!open)
{
    if (visibility_ != nullptr) { visibility_->SetOpen(false); }
    else                        { is_open_ = false; }   // <- nothing can ever undo this
}
```

`SetVisibility` is called in exactly one place — `EditorToolRowComponent::AddPanel` — so only
the Log and Console have a bound visibility. The six region panels (Viewport, World Outliner,
Actor Inspector, Camera Settings, Debug Viewer, GPU Profiler) take the `else` branch, and
`IsVisible()` then returns false forever, so the window is never submitted again.

ED1 replaced the latch with a local `bool` and a write-out, which genuinely fixed it *for
windows with a bound visibility* — that is what makes View > Log restore a closed tab. For
every other window the behaviour was unchanged and still terminal. ED1's evidence did not
distinguish the two cases.

ED2 made it visible rather than causing it: with regions resolved by the layout, a dismissed
panel leaves a permanent hole in the tiling that nothing fills.

**Fix — and the general rule it establishes:**

```cpp
// A close button is a promise that the window comes back. Only override this where
// something else can restore the window.
virtual bool HasCloseButton() const { return false; }
```

The default was `true`, which meant every window offered a close button while nothing could
reopen one. It is now `false`, and **no window in the editor overrides it**, deliberately:

- a layout-placed panel's region is always present, so dismissing it would leave a hole in
  the tiling (the same reasoning that removed the padlock in ED2);
- the loading-tree Startup Profiler and the Live2D viewer's log panel have no owner that
  could reopen them — the Live2D panel is rendered unconditionally by its host, and the
  Startup Profiler has no toggle at all.

Tool-row tabs remain the closable surface, and they route through the row's shared
`EditorWindowVisibility`, which the View menu restores. A window that later gains a real
reopen path overrides `HasCloseButton()` and the write-out path is already in place.

Chrome had to be re-gated so this did not strip the focus accent: the accent is now drawn
for every titled window, and only the padlock stays conditional on the window placing
itself. That matters for the Live2D viewer's log panel, which keeps its padlock — its
snap-back affordance — while losing a close button it could not recover from.

**Side effect:** `RenderWindowChrome()` was already dead after ED2 (its only caller had been
replaced by the split accent/toggle calls), so it was deleted rather than left as a second
path into the same drawing.

**Verified:** `ED2-vulkan-no-close-buttons.png` shows no close button on any region panel;
the only X in the workspace is on the Log tab. No window can now be closed into an
unrecoverable state. Full suite 654/655, the one failure the pre-existing LevelLoader
fixture case.

## Remaining risks and unverified areas

- The splitter glue is smoke-only. Confining it to one function is the mitigation.
- The **loading tree is unchanged**, including its pre-existing overlap: the Startup
  Profiler (`0.62, 0.06, 0.36, 0.62`, unlocked) covers part of the centred 560×250 loading
  card. Recorded in the TODO as deferred rather than fixed, because ED2 is workspace layout.
- Splitter handles overlay the seam with no gutter, so a click within ~3 px of a seam hits
  the handle rather than the panel. Chosen over gutters, which would have changed every
  panel's rect and its golden test.
- The default layout shifts the right column's rows by about 4%, because fractions are now
  of the tiling area rather than of the work area. Every horizontal coordinate is unchanged.
- The Live2D viewer host still builds an `EditorLogComponent` with the ratio path, so the
  editor has two geometry mechanisms until a later stage unifies them.

## Remaining work

ED3 owns magnetic placement: dragging a panel across the workspace with a ghost preview and
snapping into a region. The tree is the prerequisite — it supplies the enumerable set of
destinations and the parent/child relation to split — and ED1's two-destination drop ghost
is the same mechanism with a two-entry destination table. ED3 will also want to host the GPU
Profiler and Debug Viewer as tool-row panels, which the deleted geometry outputs no longer
block.
