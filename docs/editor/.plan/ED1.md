# ED1 — Tabbed Editor Tool Row

- Status: implemented (2026-09-13)
- Parent design: [Editor Module](../editor_module.md)
- Roadmap: [Editor TODO](../TODO.md)
- Implementation record: [journal](../../../.spec/journal/2026-09-13-editor-tool-row.md)
- Prerequisite for: [AB1.2 Asset Browser](../../asset/asset-browser/.plan/AB1.2.md)

## Objective

Give the Editor's bottom band a shared, tabbed **tool row** so several tool panels take
turns in one region instead of stacking opaque windows on the same pixels. A panel can be
dragged out into a standalone window when the user wants two at once, and every surface
that can hide a panel shares one visibility state.

ED1 is complete when the Log and Console panels live as tabs in one row, each tab has a
close X, a tab can be isolated and re-docked, and **View > Log** / **View > Console**
agree with the tab strip in the same frame.

## Design question

How can the Editor host several tool panels in one region, and let a panel leave and rejoin
it, without adopting a docking system the vendored ImGui does not have?

## Scope boundary

ED1 owns:

- one ImGui-free `EditorToolRowModel` holding tab order, active tab, visibility, and dock state;
- one shared `EditorWindowVisibility` value type;
- the non-latching window close, and an optional external visibility binding;
- a queried `MenuItem::is_selected` replacing the never-read `selected` flag;
- the tool row component, its hand-rolled tab strip, its two-destination drop preview, and
  the standalone windows for isolated panels;
- the **View** menu, hosting the Log and Console panels as tabs;
- the Console refactor that makes its body embeddable.

ED1 does not own: workspace regions or a splitter tree (ED2), full magnetic placement
(ED3), panel reordering, tab-strip overflow, persistence of dock state in
`config/settings.json`, or any change to which assets the Editor can see.

## Why the mechanism is hand-rolled

`third_party/imgui` is the master branch at 1.91.4 WIP: `DockSpace`,
`ImGuiConfigFlags_DockingEnable`, and `ImGuiWindowFlags_DockNodeHost` do not exist, and
`third_party/` must not be modified. `BeginTabBar`/`BeginTabItem` exist, but a tab item
owns its active index internally — which contradicts the requirement that visibility be a
single model-owned value — and provides no close button, no per-tab context menu, and no
drag-out. The strip is therefore a row of `ImGui::Button` items, which gives all four
required interactions one place to live.

Core drag-drop is available but unused here: because re-docking is an explicit menu
action, the drag resolves from item state and the global mouse release rather than a
drag-drop payload.

## Invariants

- The active entry is always unset or **open and docked**. Every model mutator ends in
  `ReconcileActiveIndex()`, so no caller can violate it.
- Entry count never changes from a visibility or dock change. An isolated tab stays in the
  strip so the View checkmark always has an entry to map to.
- One bool per panel. The tab close, the detached window's X, the View item, and the
  Console's `~` key all read and write the same `EditorWindowVisibility`.
- The row owns its panels. They never enter `EditorUI::components_`, so nothing renders
  them twice, and all three of EditorUI's teardown paths stay correct with no extra
  cleanup.
- Entry storage is a `std::deque`: panels borrow a pointer to their entry's visibility for
  their whole lifetime, so growth must not invalidate it.
- The model is ImGui-free. Its test target links only `KP::GoogleTest`; a build that names
  `imgui.h` means the layering was broken.
- The row's shape does not depend on how many tabs are open. The panel body is always laid
  out below the strip, so `SameLine()` is emitted only between tabs that are actually
  drawn — never after the last one, and never driven by the entry count.

## Migration

Panels are unaffected while unbound: `EditorWindowComponent` keeps its `is_open_` behavior
when no visibility is attached, so the seven existing windows (Viewport, World Outliner,
Actor Inspector, Camera Settings, Debug Viewer, GPU Profiler, Startup Profiler) behave
exactly as before. The Log and Console lose their own windows and their hardcoded geometry;
the row takes the Log's former band (`x 0..0.8`, `y 0.7..0.96`) and its horizontal
scrollbar flag, since the Log no longer owns a window to carry it.

The Console's `~` toggle now writes the shared visibility instead of a private flag, so the
hotkey, the tab, and the menu item cannot disagree.

## Validation

Level 3 per [the validation matrix](../../validation_matrix.md): `engine/editor/**` with
changed UI behavior. Editor target build plus editor startup/render smoke, with
`capture.screenshot` using `view: engine_window` under `save/screenshots/validation/`.

The model's tab, visibility, reconciliation, and drop-resolution rules are covered
headlessly. Widget behavior — `Begin`, clicks, drag, popups — needs a live frame and cannot
be unit-tested under the current harness; it is covered by smoke.

## Risks

- **Interactive wiring has no automated coverage.** Tab clicks, isolate, the drag preview,
  and the View toggles require input the command transport cannot inject. The model logic
  underneath them is tested; the ImGui event glue is not.
- Keyboard-nav focus is stale for one frame when a focused window stops being submitted. If
  smoke shows it, call `ImGui::SetWindowFocus(nullptr)` on the visibility flip.
- Detached windows deliberately drop the base window chrome, since the base's ratio
  geometry cannot express "wherever the user dragged it".
