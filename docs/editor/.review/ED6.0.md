# ED6.0 — Baseline and ownership freeze

- Date: 2026-11-10
- Status: baseline inventory completed; later reconstruction boundaries remain open
- Design: [ED6](../.plan/ED6.md)
- Roadmap: [Editor TODO](../TODO.md#ed6--reusable-ui-and-editor-reconstruction)
- User responsibility contract: [Editor README](../../../engine/editor/README.md)
- Repository baseline: HEAD `90f6597668c74774fcbdd3e11ed57cd0cf9545d3` plus the dirty working tree
- Source state: includes the in-progress Settings and Log editor-target changes and
  accepted ED6.1 lifecycle work. This is the freeze point before ED6.2; it is not
  a reconstructed pre-change snapshot.

## Accepted design boundary

Every module's feature editor view and every public Editor UI component uses
Editor's general toolkit-neutral UI API. The Editor ImGui backend is the only
production owner allowed to include ImGui headers or call ImGui functions.
Feature editor targets depend on Editor UI and their narrow domain runtime
contracts; feature runtime targets remain independent of Editor and UI.
Workspace and SceneEditor code use the same Editor API. This is the target
architecture; the direct ImGui uses listed below are current migration work.

## Target and include dependency baseline

| Current target | Current dependency/evidence | ED6 ownership decision |
|---|---|---|
| `EditorUILib` | Aggregates `ui`, `platform`, Log, Settings, profile, Actor, Asset and gizmo sources. Links RuntimeLib, RuntimeCommand, Input, ImageIO, ImGui, GLFW, Glad, Vulkan and JSON. `EditorUIComponent` publicly includes ImGui. | Split toolkit-neutral UI contracts/widgets from the private ImGui backend and feature tools. The source directory may remain under `engine/editor`; target boundaries establish reuse. |
| `EditorLib` | `editor.cpp`; links RuntimeLib and EditorUILib. | Keep application/tool composition out of Runtime; later split SceneEditor and neutral host lifecycle. |
| `RuntimeLib` | Links EditorLib privately; Engine includes and constructs concrete `editor::Editor`. Together with EditorLib and EditorUILib this is the known static-library cycle. | ED6.6 removes Runtime's concrete Editor edge. No runtime feature target gains UI or ImGui. |
| `LogEditor` | `engine/runtime/core/log/editor/log_editor_view.cpp`; links Log publicly and ImGui privately. Reused by Editor, Signal Lab, Terrain and Live2D call sites. | Keep filtering/selection policy in Log's editor tool; route drawing through Editor's API, with no ImGui dependency in LogEditor. |
| `AudioPlayerMode` | PUBLIC Audio/RuntimeLib; PRIVATE AssetImport, ImageIO, EditorUILib and ImGui. Compiles an Editor audio component from `engine/editor/ui/component`. | Audio tool state stays Audio-owned; general transport controls/drawing come from Editor UI. |
| `TtsEditorMode` | PUBLIC TTS/RuntimeLib; PRIVATE EditorUILib, ImageIO, ImGui and JSON. `tts_editor_editor.cpp` calls ImGui. | TTS owns speech/editor state and actions; view draws through Editor UI. |
| `SignalLabMode` | PUBLIC Signal/RuntimeLib; PRIVATE EditorUILib and ImGui. `signal_lab_editor.cpp` calls ImGui and constructs LogEditor. | Signal Lab owns signal state and visualization policy; controls/canvas and Log view use Editor UI contracts. |
| `TerrainEditor` | PRIVATE TerrainCore/Generation, RuntimeLib, EditorUILib and ImGui. `terrain_editor.cpp` and `heightmap_debug_view.cpp` call ImGui. | Terrain owns generation/editor state; viewport/debug drawing uses Editor UI canvas/image operations. |
| `Live2DModule` | PUBLIC Live2D/Bubble; PRIVATE Live2DImport, RuntimeScreenshot, EditorUILib, ImGui and JSON. Its viewer editor calls ImGui and constructs LogEditor. | Live2D owns model/viewer policy; UI and custom canvas calls route through Editor. |
| `RuntimeSettings` and `Log` runtime targets | RuntimeSettings depends on Reflection/JSON; Log depends on Config/MagicEnum. Neither runtime target links ImGui. Settings editor sources currently enter EditorUILib from `runtime/settings/editor`. | Preserve UI-free runtime ownership. Move only the editor drawing/tool target boundary as later stages authorize. |

Current direct ImGui feature-view sites outside the Editor module are Log,
Settings, Audio, TTS, Signal Lab, Terrain and Live2D. Editor-owned Actor, Asset,
gizmo, profile and generic component implementations also call ImGui today.
They are not exempt from the README contract: only backend implementation and
backend-specific tests may retain toolkit headers, types and calls after
migration. ImGui's vendored implementation and unavoidable integration stay
confined to the backend dependency.

The in-progress Settings move changes source location, not target ownership:
`runtime/settings/editor` contributes sources to EditorUILib, while
`EditorUI` constructs the wrapper and owns its lifetime. The in-progress Log
move creates a separate target but its view still includes/calls ImGui. Neither
directory placement nor a separate static library alone satisfies the new UI
boundary.

## Live component owners, callers and lifetimes

| Live operation | Current owner and caller | Lifetime/borrowed state |
|---|---|---|
| Presentation context, WSI and renderer | EditorUI initialization and platform adapters | Initialized and destroyed on the UI/render lane; window/Graphics services must outlive presentation teardown. |
| Loading tree and startup profiler | `EditorUI::loading_components_`, populated before workspace promotion | Loading-only; released when promotion succeeds or on failed close. |
| Scene panel collection and tool row | `EditorUI::components_` owns the row; `EditorToolRowComponent` owns panel windows | Created by `PromoteToWorkspace`; panels are destroyed before their borrowed Runtime services. |
| Panel placement/visibility | EditorUI owns `EditorToolRowModel` and `EditorLayoutModel`; row borrows them | IDs and current layout topology are SceneEditor policy; standalone hosts can reuse row UI but should provide their own workspace preset. |
| Actor outliner/inspector | EditorUI owns `ActorEditorModel`; two panel windows borrow it | Built only when reflection catalog, snapshot source and edit sink are all published; teardown precedes borrowed service teardown. |
| Asset browser/reference | EditorUI owns browser/reference models and shared reference visibility; panels borrow models and callbacks | Asset catalog snapshot source is borrowed; reference panel shares its visibility state with the workspace command path. |
| Log viewer | Each `EditorLogComponent` owns a `LogEditorView`; view borrows LogSystem/snapshot access | Constructed in several application hosts; each view's filters and selection are host-local. Current draw implementation is ImGui-bound. |
| Command Console | EditorUI constructs the component; it borrows CommandRegistry and InputSystem | Hidden updates drain completions; component must be destroyed before those services. |
| Debug Viewer / GPU Profiler | EditorUI owns panel components; viewer borrows RenderSystem/renderer, profiler borrows Engine/RenderSystem/EditorUI | Viewer demand and profiler reads follow component/workspace lifetime; no GPU object moves into the tools. |
| Settings window | EditorUI owns the concrete wrapper; wrapper owns its model/view, which consume copied snapshots and a borrowed Settings access interface | Deferred Apply/Discard/Cancel close belongs to the Settings tool; host commits visibility only after its decision. |
| Profile bar | EditorUI owns component and its `unique_ptr<EditorMetric>` collection | Metric samplers borrow engine/runtime telemetry sources; component is destroyed before those sources. |
| Standalone tool shells | Application modes own their feature editor/session objects; they reuse selected Editor UI components | Audio, TTS, Signal, Terrain and Live2D do not share one global workspace instance or feature model. Their current UI drawing is nevertheless toolkit-bound. |

`EditorContainerComponent` is compiled but never constructed and is explicitly
listed as dead code in `docs/dead_code.md`; it is not a live owner or a migration
consumer. The global extension registry has a declaration, implementation and
one EditorUI snapshot consumer, but no production registration caller was found.
It stores anonymous factories rather than tool identity, placement, menu actions
or a workspace lifetime contract.

## Layouts, commands and saved formats

- Scene panel IDs: `viewport`, `world_outliner`, `actor_inspector`,
  `camera_settings`, `debug_viewer`, `gpu_profiler`, `asset_browser`, `log`, and
  `console`. Actor panels are omitted when Actor services are unavailable.
  Asset Browser and Console start closed; the other registered panels start open
  unless restored placement state overrides the default.
- `save/editor_layout.json` is version 3. Split sizes are keyed by stable split
  names (`left_column`, `right_column`, `outliner`, `tool_row`, `camera`,
  `debug`); panel placements are keyed by panel ID and contain dock key plus
  lock state. Empty dock means floating. Versions 1 and 2 remain readable; v2
  region-keyed records are converted, and v3 is written atomically.
- No second shared Editor layout path was found for standalone workspaces.
  Their current fixed layouts are assembled by each application. Existing
  representative visual references are `audio-player-layout.png`, the
  `signal-lab-*-2026-11-08.png` pair, `tts-controls-row-20261003.png`, and
  `terrain-tp3-profilebar-vulkan.png`. The Scene Editor was visibly inspected
  during this baseline; its current file-backed layout is the v3 file above.
- The registered Editor panel commands are `editor.panel.list`,
  `editor.panel.show(id)`, and `editor.panel.focus(id)`. They enqueue value
  requests to the UI lane; the bounded queue reports pending, terminal and
  shutdown outcomes. Panel IDs above are the command IDs and persisted IDs.
- Tool-row dock/floating windows use `ImGuiWindowFlags_NoSavedSettings`; source
  does not override ImGui's default `IniFilename` globally. The explicit,
  versioned `save/editor_layout.json` is authoritative for SceneEditor splits
  and panel placements. Other ImGui windows may still use the backend's default
  ini behavior until the neutral API owns window persistence.

## Debug correctness and RelWithDebInfo UI timing baseline

- Working tree at HEAD `90f6597668c74774fcbdd3e11ed57cd0cf9545d3` contained
  existing Settings/Log feature work, ED6 planning, and ED6.1 implementation.
  This record freezes that state before ED6.2; it does not claim a pristine or
  pre-ED6.1 comparison.
- Debug build passed for `EditorUILib`, `KimPeanutEngine`, and nine Editor test
  targets. Direct GoogleTest runs passed 150/150 tests: UI lifecycle 6,
  loading model 4, Actor model 3, tool-row/content model 51, panel commands 4,
  Settings model 5, layout/persistence 47, Asset browser model 22 and Asset
  reference model 8. The first layout persistence run hit the sandbox's
  redirected Temp permissions; rerunning with `TEMP`/`TMP` under `build/`
  passed all 47. These tests are the Debug correctness baseline; no Debug
  Vulkan-validation runtime result is claimed here.
- RelWithDebInfo `KimPeanutEngine` build passed. MSVC emitted warning LNK4098
  (MSVCRTD default-library conflict); it did not fail the link.
- Visible timing fixture: current default Sponza scene and camera, Vulkan,
  1283x751 engine window and approximately 729x420 scene viewport, path tracing
  active, texture-residency tracker complete after a 25-second warm-up. The
  exact startup fixture path and camera transform were not independently
  queried. Vulkan validation was not used for performance sampling.
- A five-read visible sample window covered about 15 seconds. One clearly
  readable instantaneous profiler row was ImGui total 1.31 ms (build 0.62 ms,
  submit 0.70 ms). These are existing CPU function-scope timings, not isolated
  widget cost or GPU execution; no percentile/average is claimed from the
  screenshots. The tool/profile state was changed only to reveal this panel;
  scene settings and saved layout were not edited.
- Historical saved references cover representative Audio Player, Signal Lab,
  TTS and Terrain UI. A fresh Live2D shell image was not present in the archive;
  its source composition and direct-ImGui target are inventoried above. Existing
  ED6.0 work was therefore scoped as a recorded source/layout baseline, not a
  complete cross-host visual acceptance pass. Later stages still require live
  multi-host and backend validation.

## Findings and disposition

1. **High — feature/editor modules own toolkit calls.** The new README contract
   is stronger than the current graph: LogEditor, Settings view, Audio,
   SignalLab, Terrain, TTS and Live2D call ImGui, and Editor's public base
   component includes it. **Recorded for ED6.2/ED6.5 migration; unresolved in
   code.**
2. **High — reusable Editor UI has no neutral link target yet.** The Editor
   aggregate exposes ImGui headers and links broad Runtime services; external
   editor targets link the aggregate plus ImGui directly. **ED6.2 owns API,
   backend and target separation.**
3. **High — Runtime/Editor library cycle remains.** RuntimeLib -> EditorLib ->
   EditorUILib -> RuntimeLib, plus Engine's concrete Editor construction.
   **ED6.6 owns removal.**
4. **Medium — scene topology remains in generic layout/model code.** Stable
   panel and split IDs are captured above; **ED6.3 moves presets and
   workspace-scoped persistence without losing the v1-v3 scene adapter.**
5. **Medium — standalone layout visual coverage is historical/incomplete.**
   Source composition and saved-format inventory are frozen; no fresh visual
   pass for every standalone host was possible through the available app launch
   API (it launches the executable without command-line mode arguments).
   **ED6.7 still requires visible host/backend acceptance.**

## Validation

- Static scans covered CMake target sources/links, direct ImGui include/type/call
  sites, extension registration callers, panel constructors/IDs, layout paths,
  command descriptors and `docs/dead_code.md`.
- Debug builds/tests and RelWithDebInfo engine build/results are listed above.
- `git diff --check` and documentation links are recorded in the ED6 journal.
- No source was moved or refactored for this baseline. ED6.2-ED6.7 remain open;
  ED5 and SET3 acceptance remain independent.
