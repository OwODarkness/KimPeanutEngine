# Editor documentation and implementation guide

Follow the root [agent contract](../../AGENTS.md) and
[validation matrix](../validation_matrix.md).

- [PLANS.md](PLANS.md) maps architecture and proposed extensions;
  [editor_module.md](editor_module.md) describes existing editor behavior.
- [TODO.md](TODO.md) owns stage status and acceptance. A design document does
  not mean that the corresponding feature is implemented.
- Components own UI state and submit presentation data. They never own GPU
  objects or access native graphics contexts.
- Only the Editor ImGui backend may own ImGui integration. Public Editor UI
  contracts and all feature/workspace views use toolkit-neutral framework types.
  `EditorUILib` is currently a legacy aggregate and still contains direct ImGui
  calls outside the backend; ED6.2/ED6.5 migrate them. Render owns scene rendering
  and the frame schedule; Graphics owns physical GPU objects, synchronization,
  submission, and presentation.
- ED6's target boundary requires feature/SceneEditor/workspace views to use
  Editor's general UI API. Keep ImGui calls, types, flags and headers inside the
  backend implementation and its implementation-specific tests. Public neutral
  UI headers expose framework-owned types only. Existing direct uses are legacy
  migration work; do not extend them as the final API. A future stage still needs
  its own implementation authorization under the execution spec.
- The general UI API owns reusable control behavior and neutral style mechanisms
  (semantic text roles, colors, spacing, borders, and related presentation tokens).
  Feature-specific visual identity belongs to that feature's Editor implementation;
  do not hardcode a module's palette or branded control styling into general controls.
  A feature may apply its own theme through the neutral API, while the backend maps
  those tokens to its toolkit. Avoid module-specific branches in the shared backend.
- Extend the existing editor-composite seam explicitly when editor presentation
  needs GPU work. Keep effect policy and ImGui types out of common RHI contracts.
- Preserve loading-safe presentation before a scene renderer exists. Preserve
  the known RuntimeLib/EditorLib boundary as legacy debt; do not add to the cycle.
  ED6.6 owns removing Runtime's concrete Editor dependency. Preserve all third-
  party sources.
- UI rendering changes require visible Vulkan/OpenGL evidence, including
  `engine_window` captures. Use the approved external-desktop launch path;
  compilation alone does not validate visual behavior or lifetime.

Create an execution spec before starting a substantial ED stage implementation;
record actual validation and limitations in its dated journal.

## Editor purpose and current dependencies

Editor is the reusable tooling layer: general toolkit-neutral controls/windows,
with ImGui as the current backend, optional
workspace hosting, snapshot-to-view and intent-routing mechanisms, presentation,
UI diagnostics, and tool lifecycle. A Scene Editor is one application
composition that adds scene layout, viewport interaction, selection/gizmos, and
chosen feature tools. Audio Player, TTS, Terrain, Signal Lab, and Live2D are
separate hosts that currently reuse Editor UI code.

The current target graph has a known cycle:

```text
RuntimeLib <----> EditorLib ----> EditorUILib
     ^                               |
     +-------------------------------+
```

`RuntimeLib` links `EditorLib`; `EditorLib` links both `RuntimeLib` and
`EditorUILib`; `EditorUILib` links `RuntimeLib` and UI/platform dependencies.
`Engine` constructs `editor::Editor` and calls its UI lifecycle. Treat this as
legacy coupling, not an example for new runtime code.

These application/feature targets also link `EditorUILib` directly:

- `AudioPlayerMode` — EditorUI, tool-row/window components, audio/log/profile
  panels, and shared transport widgets.
- `SignalLabMode` — EditorUI and the Log panel.
- `TtsEditorMode` — EditorUI and shared audio transport widgets.
- `TerrainEditor` — EditorUI, tool-row/layout, Log, and profiler panels.
- `Live2DModule` — EditorUI initialization/settings and Live2D-specific tools.

Feature editor code may depend on domain runtime contracts and reusable UI.
Runtime feature targets must stay independent of ImGui and their editor targets.
Source directory placement does not create a target boundary: inspect
`target_sources` and `target_link_libraries`. Graphics' `IEditorPresentationBridge`
and Render's editor-composite hooks are lower-level presentation contracts;
they do not link the Editor library.

ED6's intended direction is an Editor-owned toolkit-neutral UI API and private
ImGui backend, optional generic workspace hosting, feature-owned editor targets,
and SceneEditor composition. Read [ED6](.plan/ED6.md), its
[architecture review](.review/ED6.md), and the
[ED6.0 baseline](.review/ED6.0.md) before moving owners. ED6.0's source/build
inventory and baseline sample are frozen; toolkit migration and final matched
performance/visual acceptance remain open.
