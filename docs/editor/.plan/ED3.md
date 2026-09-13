# ED3 — Magnetic Placement

- Status: implemented (2026-09-13)
- Parent design: [Editor Module](../editor_module.md)
- Roadmap: [Editor TODO](../TODO.md)
- Builds on: [ED1](ED1.md) (the tool row), [ED2](ED2.md) (the layout model)
- Implementation record: [journal](../../../.spec/journal/2026-09-13-editor-magnetic-placement.md)

## Objective

Let a tool-row panel leave the strip and take over a free workspace region, previewing the
destination while it is dragged. ED3 is complete when dragging a tab over a free region
shows that region as the drop target, releasing pins the panel there, and the arrangement
survives a relaunch.

## Design question

How can a panel move between the tool row and a workspace region, with a ghost preview and
a snap, without the editor adopting a docking system — and without either ownership regime
learning about the other?

## Scope boundary

ED3 owns:

- region identity: stable keys, and the declaration of which regions are free;
- a half-open region hit test, so a point belongs to exactly one region;
- per-entry placement state in the tool-row model, and the pure drop rule over it;
- the pinned rendering branch, the dashed drop targets, and the region ghost;
- hosting the Debug Viewer and Performance Profiler as tool-row panels;
- persisted placements, with version 1 files still read;
- a **View menu generated from the registered panels** rather than written out by hand. This
  was not in the original scope and is a defect fix: the two hardcoded items left the new tabs
  with no entry, so a closed one could not be reopened.

ED3 does **not** own: displacing a panel that already occupies a region, region splitting,
a drag source on a layout-owned window's title bar, tab reordering, or strip overflow.

## Why it is small

The two ownership regimes were the reason this stage could have been large. A drop that
**displaced** a region panel would move a `unique_ptr` between `EditorToolRowComponent::panels_`
and `EditorUI::components_`, re-bind each panel's `EditorWindowVisibility`, add and remove
model entries, and keep three teardown paths correct.

Instead, **a drop never displaces anything**. Only regions with no permanent occupant are
destinations, so nothing has to move between owners: a pinned panel is still a row panel,
still owned by the row, still drawn by the row's existing detached-window path. The only
change is *where* that path puts the window. The destination set is created by doing the
TODO's third bullet — moving the Debug Viewer and Performance Profiler into the row frees
their two regions.

## Invariants

- **`EditorLayoutSlot` names a region, not a panel.** Most regions have one permanent
  occupant with the same name, which is why the spellings match, but two of them have none.
- **A drop never displaces a panel.** `PlaceInRegion` refuses an occupied region, and
  `IsRegionPlaceable` keeps a region with a permanent occupant out of the destination set
  entirely, so the two panels can never be drawn into one rectangle.
- **Docking clears the region.** One entry cannot be in two places, so `SetDocked(index, true)`
  resets it. That single line is what makes the shipped "Dock to tool row" menu bar and
  `ShowInRowById` the return path for a pinned panel — the ED1 rule, extended to a third
  way of leaving the strip.
- **The drop rule is one pure function.** `ResolvePlacementDrop` decides everything;
  the component only draws what it says and applies what it decides.
- **Regions partition the plane.** The hit test is half-open on the far edges, unlike
  `EditorRect::Contains`. Tiling regions share edges, and an inclusive test would let
  whichever sits earlier in the enum claim every seam.
- **A drag always ends.** The release edge is checked before any early return, so a release
  nowhere in particular still clears the gesture.
- **Placement is persisted on change, never per frame.** The revision lives in the model, so
  the editor needs no pointer to the component.
- Both models stay ImGui-free; the two test targets compile the sources directly, so an
  accidental `imgui.h` include is a build error.

## Migration

The Debug Viewer and Performance Profiler stop being region panels and become tool-row
panels. Their constructors stop passing `SlotConfig(slot)`, because a panel hosted by the row
must not declare a slot — the row draws its body inside its own window, and a slot would put
it in the per-frame layout pass as well and render it twice. Their two slots survive in the
enum as region ids with no occupant.

The right column is therefore Camera Settings plus two free regions. Dragging either tab back
into the region it came from restores the old arrangement.

`EditorLayoutState` gains a `placements` map and version 2. Version 1 is still accepted and
read as "no placements": it is a format this build knows, and rejecting it would discard a
user's saved split sizes on upgrade for nothing.

## Validation

Level 3 per [the validation matrix](../../validation_matrix.md). Editor smoke on both
backends, with captures under `save/screenshots/validation/`.

**The placement path is verifiable through the command transport**, which is why it is
persisted at all: writing `{"version": 2, "placements": {"gpu_profiler": "log"}}` to
`save/editor_layout.json` and relaunching reproduces a pinned panel with no input injection.
That is the same lever ED2 used for its resize path, and it is the required evidence — the
drag gesture itself remains smoke-only.

## Risks

- **The drag gesture has no automated coverage.** Unchanged from ED1 and ED2. The mitigation
  is that every rule is a pure tested function and the component only draws.
- **Two free regions are the visible cost of the model.** Magnetic placement needs somewhere
  to put a panel; the alternative is displacing an occupant. Region *splitting* — dropping on
  a region's edge to give the panel half of it — is what removes the need for free space, and
  it needs the layout tree to grow at runtime. That is its own stage.
- **The row now holds four tabs** (three visible by default). The strip still has no overflow
  scrolling, so a fifth would start to clip at 1280 px. Already a TODO entry.
- The two default free regions are ~65% of the right column, which is a large empty area
  until something is dropped in. Deliberate and labelled rather than hidden.
- **The View menu's contents are unverified by frame** — opening it needs a mouse. The item
  set is generated from the tool row, so it cannot drift, but nobody has watched it open.
