# ED2 — Editor Layout Model

- Status: implemented (2026-09-13)
- Parent design: [Editor Module](../editor_module.md)
- Roadmap: [Editor TODO](../TODO.md)
- Implementation record: [journal](../../../.spec/journal/2026-09-13-editor-layout.md)
- Builds on: [ED1 tabbed tool row](ED1.md)
- Prerequisite for: ED3 magnetic placement

## Objective

Replace per-panel hardcoded geometry ratios with one explicit layout model: named regions
in a splitter tree, resolved to rectangles once per frame, resizable through draggable
seams, and persisted across launches.

ED2 is complete when no workspace panel carries a geometry literal, resizing one region
reflows its siblings, the tool row's top edge is draggable, a resized layout survives a
relaunch, and no two regions overlap or leave a gap.

## Design question

How can panels be laid out so the editor knows the set of regions, without adopting a
docking system the vendored ImGui does not have?

## Why this was needed

Three independent hardcoded ratios described the bottom band, and they disagreed:

| Region | Old geometry | Consequence |
| --- | --- | --- |
| GPU Profiler | `y 0.70` + `h 0.34` = **1.04** | overflowed the work area by 4% and drew over the Profile bar's Memory metric; ImGui does not clamp an API-set position |
| Tool row | bottom at `0.96·H` | overlapped the bar by ~3 px below 1075 px tall, left a **13 px gap** above it |
| Profile bar | `H − 43 px` (content-derived) | the only one that was right |

Nothing could catch this, because no code knew the set of regions and so nothing could
assert they tile. ED2 introduces that place, and the tiling assertion is now a test.

A second gap followed from the same cause: the tool row could not be resized, because its
rect was a literal nobody else could reason about.

## The tree

Leaves are slots; internal nodes are splits. The structure is a **declarative table**, not
hand-written nesting code, so adding a region is a row rather than new arithmetic.

```text
root ── SplitY[StatusBar, FIXED 43 px, not draggable, not persisted]
          ├─ first  = workspace
          └─ second = ProfileBar
        workspace ── SplitX[RightColumn 0.80, min 240/160]
          ├─ first  = left-of-right
          └─ second = right column
        left-of-right ── SplitY[ToolRow 0.70, min 160/100]     ← draggable
          ├─ first  = top area
          └─ second = ToolRow
        top area ── SplitX[LeftColumn 0.275, min 140/200]      ← draggable
          ├─ first  = left column ── SplitY[Outliner 0.40]     ← draggable
          │            → {WorldOutliner, ActorInspector}
          └─ second = Viewport
        right column ── SplitY[Camera 0.35, min 80/120]        ← draggable
          ├─ first  = CameraSettings
          └─ second = lower right ── SplitY[Debug 0.5385]      ← draggable
                       → {DebugViewer, GpuProfiler}
```

8 leaves, 7 splits, 6 draggable. Fractions are **parent-relative**, which is what makes
reflow automatic: a child's rect derives from its parent's, so resizing the window or
moving any seam updates everything below it with no bookkeeping.

The nesting is not uniform — the tool row spans the left and centre columns but not the
right one — which is exactly the L-shape a flat "column widths" model handles badly. The
tool row's bottom is now the workspace bottom, so it meets the status bar by construction
rather than by a ratio that happened to agree at one window height.

**Why a tree rather than flat layout variables.** For eight leaves a flat model is less
code today. The tree earns its cost twice over: it makes the structure data; and ED3 needs
an enumerable set of destinations with a parent/child relation to snap into and split. A
flat model would have to be replaced to build ED3.

## Invariants

- **No workspace panel carries geometry.** A slotted panel's rect comes from the layout
  every frame; its `EditorWindowConfig` ratios are ignored. Panels use `SlotConfig(slot)`,
  which carries no ratio at all, so no dead geometry survives to read as live.
- **One clamp, used twice.** The resolver and the splitter drag both go through
  `ClampFirstExtent`. A stored fraction can fall below a minimum without any drag — resizing
  the window changes the extent a fraction is measured against — so clamping only on the
  drag path would let a panel resolve below its minimum.
- **The children always fill their parent.** A split computes the second child as
  `parent − first`, so tiling is exact even when a parent is too small for both minima; in
  that case the deficit is shared in proportion to the minima rather than starving one child.
- **Every geometry decision is ImGui-free and tested.** `editor_layout_model.cpp` and
  `editor_layout_settings.cpp` never include `imgui.h`; the test target links no ImGui. A
  build that names ImGui means the layering broke.
- **The layout is pushed, not queried.** `EditorUIComponent` gained
  `GetLayoutSlot()`/`ApplyLayout(std::optional<EditorRect>)`, and `EditorUI` resolves once
  and pushes a rect into each component that declares a slot. `ApplyLayout` takes an
  optional so a component that stops being slotted cannot keep a stale rect forever.
  Only components in the workspace tree may declare a slot: the tool row's hosted panels
  must not, because the row draws them inside its own window.
- **A layout-placed window is not movable and not closable.** It is always pinned, and it
  loses both the padlock and the title-bar close button. The padlock would mean "unlock to
  move freely" for a window the layout owns; the close button would dismiss a region whose
  space nothing can then fill. `locked`/`locked_`/the padlock **remain** for windows that
  place themselves — the Live2D viewer's log panel and the loading-tree Startup Profiler —
  so nothing outside `engine/editor` changed.
- **`HasCloseButton()` defaults to false**, on the rule that a close button is a promise the
  window comes back. Nothing in the editor overrides it: region panels have a permanent
  region, and the two self-placing windows have no owner that could reopen them. Tool-row
  tabs are the closable surface and route through the row's shared visibility, which the
  View menu restores. This replaced a default of `true` that let every window be closed
  into an unrecoverable state — see the correction in the implementation journal.
- **`imgui.ini` no longer holds workspace geometry.** Slotted windows carry
  `NoSavedSettings`, which also stops a rect re-pushed every frame from being written on
  every frame of a splitter drag. Verified: with `imgui.ini` deleted the workspace is
  pixel-identical, and the regenerated file contains only ImGui's default window and the
  unslotted Startup Profiler.

## Splitters

Hand-rolled rather than `ImGui::SplitterBehavior` from `imgui_internal.h`: that clamp lives
in vendored code and is unreachable from a test, and the editor currently includes ImGui
internals nowhere. The arithmetic is a pure model function; the glue is hit-testing, a
cursor, and a drag accumulator.

The handles are **not windows**. They are strips of the foreground draw list plus a mouse
test, evaluated after every panel has rendered. A host window would have brought two bugs:
ImGui routes the mouse wheel to the hovered window, so a full-area overlay would stop every
panel scrolling; and clicking a panel title raises it, so a panel would end up permanently
above the handles.

Because a handle lies on a shared edge, it is always over panel content. The precedence is
resolved by requiring no item to be hovered or active before a drag starts: over the tool
row's tab strip or the console's input the **panel wins**, and over plain background the
**splitter wins**.

A drag records the fraction at drag *start* and applies the total displacement from it, so
dragging past a minimum and back tracks the mouse again instead of latching the boundary to
the clamp.

## Persistence

`save/editor_layout.json`, keyed by splitter ID and never by index, so inserting or
removing a split in ED3 cannot silently reattach a saved size to a different seam:

```json
{ "version": 1, "splits": { "tool_row": { "amount": 0.45 } } }
```

It is its own file and NOT `config/settings.json`, which is checked in and parsed by two
independent readers — the editor's, and live2d's for `window_background_color` — so a
runtime rewrite there would clobber another subsystem's key. `save/` is the git-ignored
generated-output tree, reached by new `GetSaveDirectory()`/`GetEditorLayoutPath()` getters.

Written atomically (create the parent, write a unique temp, rename) on splitter release
rather than per frame. Reading is tolerant: a missing file, malformed JSON, an unknown
version, an unknown key, a non-numeric or out-of-range amount all fall back to defaults
with a diagnostic, because a broken layout preference must never stop the editor starting.
The status bar's height is never persisted — it is a font- and theme-derived measurement.

## Validation

Level 3 per [the validation matrix](../../validation_matrix.md).

`EditorLayoutModelUnitTest` — 31 tests, linking only `KP::GoogleTest` and
`KP::NlohmannJson`, no ImGui:

- golden defaults, including the status bar's 43 px and the tool row meeting it exactly;
- **tiling**: across six work-area sizes, no region escapes the work area, no two overlap,
  and their areas sum to the work area. This is the assertion that would have caught the
  GPU Profiler overflow;
- reflow: widths scale exactly with the work area while heights deliberately do not,
  because the status bar takes its fixed pixels first; dragging one seam leaves every
  region outside its subtree byte-identical;
- clamping: minima held wherever they fit, proportional squeeze when they cannot, a split
  never inverted, degenerate and NaN extents inert;
- drag: past a minimum pins, and dragging back tracks the mouse rather than latching;
- degenerate work areas (zero, 1x1, inverted, shorter than the bar, menu-bar offset);
- persistence: round-trip, missing/malformed/unknown-version/out-of-range handling, that
  content-derived splits are skipped, and that no temp file survives a write.

Editor smoke, both backends, captures under `save/screenshots/validation/`:
`ED2-vulkan-default-layout.png`, `ED2-opengl-default-layout.png`,
`ED2-vulkan-no-ini.png`, `ED2-vulkan-saved-layout.png`.

**The resize path is verifiable through the transport alone**, which ED1's interactive
behaviour was not: writing a layout file with non-default fractions and relaunching
reproduces the resized layout, so persistence and reflow are confirmed without input
injection. What still needs a hand on the mouse is dragging a seam itself and the hover
cursor.

## Risks

- **Splitter interaction has no automated coverage** — the same class of risk ED1
  documented. The arithmetic is tested; the ~60 lines of ImGui glue is smoke-only. It is
  deliberately confined to one function.
- **Two pre-existing bugs were in scope only as a side effect.** The GPU Profiler overflow
  and the tool row's band disagreement are fixed by construction. The **loading tree's**
  overlap (the Startup Profiler covers part of the loading card) is untouched: ED2 is
  workspace layout, and the loading tree lives for seconds.
- **The seam between two panels is one border wide per window**, unchanged from before.
  Rect edges are snapped so neighbours agree on the pixel of the edge they share; without
  that, fractional geometry would draw a one-pixel background line at every seam.
- **The default layout visibly differs**: the right column's rows are ~4% shorter, because
  fractions are now of the tiling area (work area minus the status bar) rather than of the
  work area. Every horizontal coordinate is unchanged.
