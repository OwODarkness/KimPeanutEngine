# Feature editor UI migration

- Status: active
- Owner: Codex
- Parent TODO: [ED6 reusable UI and Editor reconstruction](../../docs/editor/TODO.md#ed6--reusable-ui-and-editor-reconstruction)

## Objective

Use the ED6.0 ownership inventory to move remaining feature-owned Editor views
onto the neutral Editor UI API. Feature views must not include ImGui headers or
call ImGui functions, and their targets must not link `KP::ImGui` directly.

## Current state

ED6.0 identified Log, Settings, Audio, TTS, Signal Lab, Terrain and Live2D as
direct-ImGui feature views. Settings, Audio, TTS, Signal Lab, Terrain and
Live2D now use the current UI framework. Log is shared by Editor, Signal Lab,
Terrain and Live2D. Signal Lab uses Editor controls,
windows, tables and Plot while retaining signal-generation, acquisition and
palette policy in its module. Terrain's heightmap preview also uses Editor
Canvas, with terrain-owned elevation colors and bounded 128x128 sampling.
Terrain now uses neutral workspace windows, controls, diagnostics, Canvas
heightmaps and the opaque render-target image path. Live2D uses neutral
windows, tabs, controls, diagnostics and a Canvas image primitive. The Editor
owns the render-target source registry and maps opaque `UiImageId`
values to its private presentation backend; the common UI API contains no
Graphics type or native image handle.

## Scope and invariants

- Keep feature state, domain decisions, and action callbacks in their existing
  feature owners.
- Use Editor's neutral UI types for controls, windows, plots, and image/list
  presentation. Put ImGui calls only in the Editor backend.
- Add a general capability only when a current consumer requires it. Keep
  specialized Live2D, terrain, and signal policies in their modules.
- Preserve Runtime independence from Editor and UI.
- Preserve useful interactions and bounded per-frame work when replacing
  toolkit-specific drawing.
- Preserve unrelated worktree changes and do not commit as part of this goal.

## Stages

1. [x] Migrate the shared Log view and its Editor/feature hosts; remove the
   LogEditor ImGui dependency.
2. [x] Migrate TTS controls and waveform presentation; remove its direct ImGui
   dependency. Its module theme remains locally selected through UiTheme.
3. [x] Migrate Live2D controls, render-target presentation, and diagnostics;
   remove its direct ImGui dependency and Editor-component dependency.
4. [x] Migrate Signal Lab controls, plots/canvases, and log presentation;
   remove its direct ImGui dependency.
5. [x] Migrate Terrain controls, heightmap image presentation, and log
   presentation; remove its direct ImGui dependency.
6. [x] Re-scan the ED6.0 feature-view inventory, build enabled targets, update
   the ED6 journal, and report remaining Editor-internal migration work
   separately.

## Acceptance criteria

- [x] No feature-owned view in the scope directly includes or calls ImGui.
- [x] `LogEditor`, `TtsEditorMode`, `Live2DModule`, `SignalLabMode`, and
      `TerrainEditor` do not link `KP::ImGui` directly.
- [x] UI capabilities added to support these migrations have neutral public
      contracts and backend-private ImGui implementations.
- [x] Existing feature state/action ownership and Runtime boundaries remain.
- [ ] Debug engine and optional-feature builds pass; UI lifecycle and key
      feature behavior receive visible review where the approved launch path is
      available. Builds pass; visible review remains outstanding.

## Validation plan

- Run the placement audit before and after each owner migration.
- Build the affected targets serially in Debug and run `git diff --check`.
- Scan affected feature sources and CMake for `ImGui`, `imgui.h`, ImGui types,
  and direct `KP::ImGui` links.
- Do not claim visual acceptance from compilation alone; record unverified
  interaction paths and launch constraints.

## Risks and open questions

- Render-target images now use an opaque `UiImageId` in the common UI contract;
  EditorUI owns the borrowed Graphics view registry and the ImGui resolver.
- Log lists are virtualized today; the neutral API must preserve bounded row
  work rather than eagerly formatting every entry.
- Historical standalone layouts are the only visual references for some tools.
- The ED6.6 Runtime-to-Editor link cycle and Editor-internal ImGui tools remain
  outside these feature-target milestones.
