# Editor TODO

**Status: ED1–ED3 implemented; ED4 loading wireframe present; ED5 implementation in progress, acceptance open.** Architecture and stage designs are mapped in [PLANS.md](PLANS.md); landed editor design is in [editor_module.md](editor_module.md). Parent work remains in the root [status ledger](../status.md).

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

- [x] **[ED2 — Editor layout model](.plan/ED2.md).** → [journal](../../.spec/journal/2026-09-13-editor-layout.md)

  - [x] Introduce explicit regions and a splitter tree so panel geometry stops being a hardcoded ratio in each component.
  - [x] Reflow siblings when one region is resized.
  - [x] Add a resizable splitter for the tool row.
  - [x] Persist layout state rather than relying on `imgui.ini` geometry alone.
  - [x] Keep every geometry decision in an ImGui-free model so tiling, reflow, and clamping are unit-tested.

- [x] **[ED3 — Magnetic placement](.plan/ED3.md).** → [journal](../../.spec/journal/2026-09-13-editor-magnetic-placement.md)

  - [x] Drag a tool-row panel across the workspace with a ghost preview of the target region.
  - [x] Snap on drop into the resolved region, leaving the strip.
  - [x] Host the GPU Profiler and Debug Viewer as tool-row panels, freeing their two regions.
  - [x] Persist placements, with version 1 layout files still readable.
  - [x] Cover region identity, placeability, and the whole drop rule with headless tests.

- [ ] **[ED4 — Loading Wireframe Icosahedron](.plan/ED4.md).**

  - [ ] Add a loading-safe 12-vertex, 20-face, 30-edge projected wireframe model.
  - [ ] Draw rear and front edge layers with ImGui in the unused left visual rail.
  - [ ] Suppress the visual responsively before it can overlap loading telemetry.
  - [ ] Preserve a presentation-local seam for a later bounded fault/glitch style.
  - [ ] Validate active-loading, narrow-window, and failure states on Vulkan and OpenGL.

## ED5 — Selective editor UI bloom

Canonical design: [ED5](.plan/ED5.md); active execution contract:
[spec](../../.spec/specs/editor-selective-ui-bloom.md). ImGui core and vendored backend sources
remain untouched. Editor owns effect policy and UI presentation; Render owns
the frame schedule, and Graphics owns physical GPU objects and synchronization.
Progress journal: [2026-10-02](../../.spec/journal/2026-10-02-editor-selective-ui-bloom.md).

- [ ] **ED5.1 — Contracts and baseline:** freeze color/callback/texture rules,
  loading-safe frame access, copied markers, and Vulkan/OpenGL baseline captures.
- [ ] **ED5.2 — RHI UI adapter:** establish zero-emission UI/image/input parity,
  correct linear presentation, uploads/offsets, and safe resize/teardown.
- [ ] **ED5.3 — Small bloom slice:** one bounded emission region, half-resolution
  filtering, ordered halo composition, global disable, and both-API captures.
- [ ] **ED5.4 — Integration and acceptance:** theme presets, Log text, loading
  wire and selected label, budget/failure behavior, retirement stress, and
  matched RelWithDebInfo performance evidence.

The common Editor adapter now copies ImGui geometry and marker payloads, uploads
the font, presents a linear color canvas through Graphics, and records bounded
emission, half-resolution Gaussian filtering, and ordered halo composition.
Loading wire/status, the selected World Outliner row, and visible Log text
provide explicit source regions; the Tool menu's **UI Glow** switch disables
them. Log rows keep their configured colors and selection backgrounds stay
crisp. Runtime captures show the Log text bloom on Vulkan and OpenGL. OpenGL
currently differs in UI color and needs correction before cross-backend
acceptance. Retirement stress, selected-row visual verification, and
RelWithDebInfo measurements remain open.

Acceptance requires readable base UI, explicit emission only, clipped sources,
later-window occlusion, no accidental scene-image bloom, and loading presentation
independent of scene readiness. No implementation/runtime result is claimed.

## Deferred follow-up

- Tab reordering and tab-strip overflow scrolling (the hand-rolled strip has neither).
- Detached windows currently lose the base window chrome (lock toggle, focus accent).
- The **loading tree** still places itself with ratios: `EditorLoadingComponent` is a
  centred fixed-size card and `EditorStartupProfilerComponent` is the one unlocked window
  left. The two overlap today (the profiler covers part of the card). ED2 left them alone
  as out of scope; unifying them onto a layout instance is a later stage.
- The Live2D viewer host builds an `EditorLogComponent` outside `EditorUI`, so that panel
  keeps the ratio path. Nothing outside `engine/editor` was changed to accommodate ED2.
- Splitter handles sit on the seam with no gutter, so a click within ~3 px of a seam
  reaches the handle rather than the panel beneath it. Chosen over gutters, which would
  change every panel's rect.
- **A drop never displaces a panel**, so the only destinations are the regions with no
  permanent occupant — the two the Debug Viewer and Performance Profiler left in ED3. A drop
  onto an occupied region floats the panel instead. Displacing, or better *splitting*, an
  occupied region needs the layout tree to grow at runtime and regions to be identified
  dynamically; that is a later stage, and it is what would remove the need for free space.
- No layout editor UI beyond placement: the set of regions is still fixed by the table in
  `EditorLayoutModel`, and a region's placeability is declared there rather than derived.
