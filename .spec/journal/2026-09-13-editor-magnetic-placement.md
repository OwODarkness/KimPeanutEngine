# Editor Magnetic Placement — ED3

- Date: 2026-09-13
- Plan: [ED3](../../docs/editor/.plan/ED3.md)
- Roadmap: [Editor TODO](../../docs/editor/TODO.md)
- Builds on: [ED1](2026-09-13-editor-tool-row.md), [ED2](2026-09-13-editor-layout.md)
- Scope: pin a tool-row panel into a free workspace region, with a ghost and a snap

## Result

A tool-row tab can be dragged across the workspace, previewed against a dashed free region,
and released there to pin the panel into that region. The panel leaves the strip and is drawn
as a window inside the region's rectangle; "Dock to tool row" and **View > <panel>** bring it
back. Placements persist to `save/editor_layout.json`.

Three decisions were confirmed with the user before implementation, and they are what kept
the stage small:

- **Empty regions are the only drop targets.** A drop never displaces a panel, so no panel
  ever changes owner.
- **The return path is menus**, reusing the shipped routes rather than adding a drag source
  to a layout-owned window.
- **Placement persists**, which is also what makes it verifiable without input injection.

### Semantics settled by this stage

- **`EditorLayoutSlot` is a region, not a panel.** Two of its values have no permanent
  occupant. That is the whole mechanism: a region with nobody in it is a place a panel can go.
- **The two free regions are created by the TODO's third bullet.** Moving the Debug Viewer
  and Performance Profiler into the tool row is not a separate cleanup — it is what gives
  magnetic placement somewhere to put anything.
- **A drop never displaces.** `PlaceInRegion` refuses an occupied region and the destination
  set excludes regions with a permanent occupant, so two panels can never be drawn into one
  rectangle. This is the decision that avoided moving `unique_ptr`s between the row and the
  component tree.
- **Pinning is not selecting.** The model's invariant is that the active entry is open AND
  docked, and a pinned entry is not docked, so `PlaceInRegion` deliberately leaves the active
  tab to reconcile to a neighbour — the same outcome as isolating.
- **Docking clears the region.** `SetDocked(index, true)` resets it, which is what makes the
  already-shipped menu routes work as the return path without a line of new UI. It is ED1's
  rule ("show this panel" means "put it in the row") applied to a third way of leaving.
- **Regions partition the plane, so the hit test is half-open.** `EditorRect::Contains` is
  inclusive on both edges, which is right for "is the cursor inside the row" but wrong for a
  set of rectangles that tile: every shared seam would belong to two regions and go to
  whichever sits earlier in the enum. `HitTestPlaceableRegion` uses `[min, max)` instead, and
  the consequence is that a point on the outermost right or bottom edge of the work area
  belongs to no region and floats — the same answer as dropping just outside the window.
- **Placeability is declared, not derived.** Whether a region is free is a fact about the
  component tree, which the layout model cannot see, so `ResetToDefault` sets a parallel
  table. A panel moving into or out of the row changes that table, and a test pins the set so
  the update cannot be forgotten.
- **The revision lives in the model.** `GetPlacementRevision` is bumped by the three mutators
  that change where a panel is drawn, so `EditorUI` can persist on change without holding a
  raw pointer to a component that three teardown paths would have to clear. A visibility
  toggle deliberately does not bump it, or showing a panel would rewrite the file.
- **Version 1 of the layout file is still read.** Rejecting it would have discarded a user's
  saved split sizes on upgrade and gained nothing; it is a format this build knows, it simply
  has no placements.
- **The two halves of the file are independent.** The reader no longer returns early on a
  malformed `splits` object, so a bad split cannot cost the user a placement, or the reverse.

## Files

- `engine/editor/ui/component/editor_layout_model.h/.cpp` — region keys, `IsRegionPlaceable`,
  `HitTestPlaceableRegion`
- `engine/editor/ui/component/editor_tool_row_model.h/.cpp` — `EditorToolRowEntry::region`,
  the placement API, the revision, `ResolvePlacementDrop`
- `engine/editor/ui/component/editor_tool_row_component.h/.cpp` — the borrowed layout model,
  the pinned window branch, the dashed targets, the region ghost
- `engine/editor/settings/editor_layout_settings.h/.cpp` — placements, version 2 with version
  1 still read, `CapturePlacementState`, `ApplyPlacementState`
- `engine/editor/ui/editor_ui.h/.cpp` — two new tabs, two builders deleted, placement apply
  and save
- `engine/editor/ui/component/editor_debug_viewer_component.cpp`,
  `editor_gpu_profiler_component.cpp` — stop declaring a slot
- `engine/test/unit/editor/editor_tool_row_model_test.cpp` (24 → 43),
  `editor_layout_model_test.cpp` (31 → 43), `CMakeLists.txt`
- `docs/editor/.plan/ED3.md`, `docs/editor/TODO.md`, `docs/editor/editor_module.md`,
  `docs/status.md`

## Validation

- `cmake --build build --config Debug` — passed, no warnings from any affected unit.
- `EditorToolRowModelUnitTest` — **43/43 passed** (24 before). New cases cover pinning leaving
  the strip without closing the panel, pinning not selecting, docking clearing the region,
  `ShowInRowById` clearing it too, reopening a pinned panel keeping it pinned, refusals for an
  unknown id / `Count` / an occupied region, the revision bumping on placement changes and
  not on visibility toggles, and eleven drop-resolution cases.
- `EditorLayoutModelUnitTest` — **43/43 passed** (31 before). New cases cover placeability
  matching occupancy, region key round-trips, the hit test against placeability, the
  placement round-trip through a file, a version 1 file still loading, a partly broken file
  yielding its other half, refused placements being reported rather than applied, two panels
  not sharing a region, version 3 still rejected, and capture recording only open pinned
  panels.
- `ctest --test-dir build -C Debug` — **685/686 passed**. The single failure is the
  pre-existing `LevelLoaderTest.LoadsCheckedInGameplayLevelFixtures` (checked-in
  `pbr_showcase.level` references `model/rock1-bl/rock2`, absent from the archive).
- Editor smoke, both backends, via `capture.screenshot` with `view: engine_window`:
  - `save/screenshots/validation/ED3-vulkan-default.png`
  - `save/screenshots/validation/ED3-opengl-default.png`
  - `save/screenshots/validation/ED3-vulkan-placed-log.png`

  The defaults show the three-tab strip (`Log | Debug Viewer | Performance Profiler`) and
  both free regions labelled "Drop a panel here". Both runs wrote no layout file, which is
  the check that nothing is saved spuriously.

  Two exploratory captures are kept because the corrections below describe them rather than
  replace them: `ED3-vulkan-default-targets-invisible.png` is the state before the target
  fix, and `ED3-vulkan-unexplained-placement.png` is the run whose placement could not be
  reproduced.
- **The placement path was verified through the transport.** With
  `{"version": 2, "splits": {}, "placements": {"gpu_profiler": "log"}}` written to
  `save/editor_layout.json` and the editor relaunched, `ED3-vulkan-placed-log.png` shows the
  Log tab absent from the strip and the Log drawn inside the Performance Profiler's region
  with its "Dock to tool row" menu bar, while the Debug Viewer became the row's active tab.
  No input injection was needed, which is the same lever ED2 used for its resize path.
- `git diff --check` — clean.

### Not run

Dragging a tab with the mouse, and the hover resize of a target. Both need a hand on the
mouse. The arithmetic behind both is unit-tested, and the applied result is verified through
the persisted-placement capture above.

## Correction (2026-09-13, during validation)

**The drop targets were invisible in the first capture.** The plan had them as a 1 px dashed
outline at half alpha, and the first `ED3-vulkan-default.png` showed the two free regions as
bare background — indistinguishable from a panel that had failed to draw. The intent was
"subtle", the result was "broken".

Two changes, both from looking at the frame rather than at the code: the dashes are 2 px and
drawn at full alpha in the theme's accent colour when a drag is over them, and a faint fill
plus a dimmed **"Drop a panel here"** label is drawn always rather than only mid-drag. The
plan said the label was drag-only to avoid clutter; an unlabelled empty rectangle in the
workspace is worse clutter than a dim caption, and the region is empty whether or not a drag
is in flight.

This is the third ED-stage defect found only by looking at a live frame, after ED1's dangling
`SameLine` and the conditional `EndChild`.

## Correction 2 (2026-09-13, during validation)

**A release that resolved to `None` never ended the drag.** The gesture checked the release
edge *after* an early return for "no target this frame", so a release over nowhere left
`drag_index_` set. The ghost then disappeared — the target was still `None` — and the drag
stayed in flight indefinitely, so the *next* click anywhere resolved a drop the user had not
started. `None` is reachable whenever the row has not been laid out, which includes the first
frames and a collapsed row.

Fixed by checking the release before any early return, so the gesture always terminates:

```cpp
if (ImGui::IsMouseReleased(ImGuiMouseButton_Left))
{
    // apply, if a target resolved; then always clear
    drag_index_ = -1;
    return;
}
// only now decide whether there is anything to preview
if (layout_ == nullptr || target.kind == EditorPlacementTargetKind::None) { return; }
```

**How this was found, including what is not explained.** The first smoke run of this stage
came up with the Performance Profiler already pinned in its region, and the layout file
contained a self-placement for both panels — an arrangement nobody had asked for. Instrumenting
`PlaceInRegion`, the drag start, and the drop did **not** reproduce it: on a fresh start, with
and without a legacy `imgui.ini`, and left running for over four minutes, no probe fired and
no file was written. The only remaining path to `PlaceInRegion` is a real mouse gesture, and
the earlier run was on an attended desktop with minutes between commands, so the most likely
explanation is a hand on the mouse rather than a spontaneous event — but that is an inference,
not a measurement, and it is recorded as unresolved rather than as a fix.

The stuck-drag defect above is real, is a genuine mechanism for "a panel moved somewhere I did
not ask", and is fixed. It was found by reading the function while chasing the observation,
which is a weaker form of evidence than reproducing it, and is labelled as such.

## Correction 3 (2026-09-13, found while reviewing the return path)

**The View menu was hardcoded to two panels, so the two new tabs had no entry in it** — and a
*closed* Debug Viewer or Performance Profiler therefore had nothing that could reopen it.
Terminal, again: the same class of defect ED1 hit with its window close and ED2 hit with the
six region panels.

The hardcoded pair is the cause, not the omission. The builder even carried a comment saying
it could run before the tool row registered its panels because items were "bound by stable id
and resolved at render time" — true, and beside the point: an id that is never listed cannot be
resolved. Adding two more literal entries would fix today's symptom and guarantee a fourth
occurrence.

So the menu is now generated from the row:

```cpp
for (std::size_t index = 0; index < tool_row_model_.GetEntryCount(); ++index)
{
    const EditorToolRowEntry *const entry = tool_row_model_.GetEntry(index);
    const std::string id = entry->id;   // by value: the lambda must not borrow from an entry
    view_menu.items.push_back(MenuItem{entry->title, {}, true, /* click */, /* checked */});
}
```

This required `BuildMenuBar` to run **after** `BuildToolRow` in `PromoteToWorkspace`, which is
safe because `EditorMainMenuBarComponent` uses `BeginMainMenuBar` — its own ImGui window, drawn
at the top of the viewport regardless of where it sits in `components_`. Verified by capture:
the bar still renders and the workspace still starts below it.

Adding a tool-row panel now gets it a View entry for free, which is the property that was
missing.

**Not verified by frame:** the menu's contents. Opening it needs a mouse, and the command
transport cannot inject one, so this rests on the generation being data-driven plus a clean
startup. Recorded rather than implied.

## Remaining risks and unverified areas

- **The drag gesture is smoke-only**, as in ED1 and ED2. Confining every rule to pure tested
  functions — `ResolvePlacementDrop` above all — is the mitigation, not a solution.
- **The View menu's contents are unverified by frame**, for the same reason: opening it needs
  a mouse. The item set is generated from the row, so it cannot drift from the registered
  panels, but nobody has watched it open.
- **Two free regions are ~65% of the right column.** This is the visible cost of a design
  where a drop never displaces an occupant. It is the honest shape of the trade-off, not an
  oversight, and it is labelled.
- **Removing the need for free space means region splitting**, which requires the layout tree
  to grow at runtime and regions to be dynamically identified. That is its own stage; ED3's
  fixed tree is what makes the persistence keys simple.
- The tab strip has no overflow scrolling and the row now holds four tabs. At 1280 px they fit.
- Placement is per-checkout: the file is gitignored with the rest of `save/`, so CI and a
  fresh clone always start from the default arrangement.
