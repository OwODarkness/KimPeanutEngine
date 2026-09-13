# Editor TODO

**Status: ED1 implemented.** Landed editor design is in [editor_module.md](editor_module.md). ED2–ED3 remain proposed. Parent work remains in the root [status ledger](../status.md).

## ED — Editor shell and layout

- [x] **[ED1 — Tabbed Editor Tool Row](.plan/ED1.md).** → [journal](../../.spec/journal/2026-09-13-editor-tool-row.md)

  - [x] Add an ImGui-free `EditorToolRowModel` for tab order, active tab, visibility, and dock state.
  - [x] Add a shared `EditorWindowVisibility` so a tab close, a window close, and a View-menu checkmark are one state.
  - [x] Replace the never-read `MenuItem::selected` with a per-frame `is_selected` query.
  - [x] Fix the terminal-close latch in `EditorWindowComponent` so a closed panel can be reopened.
  - [x] Host the Log and Console panels as tabs in one bottom row.
  - [x] Refactor the Console to a window component whose body is embeddable, without losing its hidden-state command drain.
  - [x] Let a tab be isolated into a standalone window and re-docked from its context menu.
  - [x] Preview the two drop destinations while dragging a tab.
  - [x] Add the **View** menu.
  - [x] Cover the model with headless tests.

- [ ] **ED2 — Editor layout model.**

  - [ ] Introduce explicit regions and a splitter tree so panel geometry stops being a hardcoded ratio in each component.
  - [ ] Reflow siblings when one region is resized.
  - [ ] Add a resizable splitter for the tool row.
  - [ ] Persist layout state rather than relying on `imgui.ini` geometry alone.

- [ ] **ED3 — Magnetic placement.**

  - [ ] Drag any movable panel across the workspace with a ghost preview of the target region.
  - [ ] Snap on drop into the resolved region.
  - [ ] Host GPU Profiler and Debug Viewer as tool-row panels.

## Deferred follow-up

- Tab reordering and tab-strip overflow scrolling (the hand-rolled strip has neither).
- Detached windows currently lose the base window chrome (lock toggle, focus accent).
- `EditorWindowComponent::width_/height_/pos_x/pos_y` are refreshed only inside the base
  `RenderContent()`, so a panel hosted by the tool row leaves them stale. Constrains which
  panels can be hosted until ED2 gives every panel its geometry from the layout model.
