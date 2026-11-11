# Editor reconstruction review and planning — 2026-11-10

## 2026-11-11 feature-view migration goal opened

- The user asked to continue from the ED6.0 dependency inventory and migrate
  remaining module-owned ImGui views to the current neutral framework.
- Rechecked [ED6.0](../../docs/editor/.review/ED6.0.md): Log, Settings, Audio,
  TTS, Signal Lab, Terrain, and Live2D were direct-ImGui feature views. Settings
  and Audio now use the neutral API; the remaining source sites are Log,
  TTS, Signal Lab, Terrain, and Live2D.
- Direct module-target `KP::ImGui` edges remain in `LogEditor`, `TtsEditorMode`,
  `SignalLabMode`, `TerrainEditor`, and `Live2DModule`. Audio's target no longer
  links ImGui. Signal Lab, Terrain, and Live2D still instantiate the Editor-owned
  `EditorLogComponent`; the Log view is therefore the shared first migration
  candidate.
- UI capability scan: `UiContext` provides standard controls, tables, neutral
  windows, and a 2D plot/canvas. It lacks a virtualized list range and
  render-target image contract, which Log and Live2D/Terrain need respectively.
- Scope, acceptance, and ordered implementation stages are recorded in
  [the feature-view migration spec](../specs/editor-feature-ui-migration.md).
- Placement review before source edits: the Log draw operation currently sits
  in `runtime/core/log/editor/LogEditorView`; reusable list/window/clipboard
  behavior belongs in Editor UI, while log filtering, selection, and row content
  stay in LogEditor. Feature hosts own their windows and borrowed LogSystem
  lifetimes. Candidate references inspected: the Log view, Editor's wrapper,
  Signal Lab/Terrain/Live2D callers, `UiContext`, both UI backends, and ED6.0.
- Stage 1 implementation: added neutral clipboard support and a virtualized
  list range to `UiContext`/the private ImGui backend, moved Log filtering,
  formatting, coloring, and copy actions onto that API, and removed `KP::ImGui`
  from `LogEditor`. Editor, Signal Lab, Terrain, and Live2D now submit the shared
  log view through `UiContext`; feature modules no longer construct
  `EditorLogComponent`.
- The Log view currently exposes single-row selection plus copy-selected and
  copy-filtered actions. Its former drag-range selection, Ctrl+C shortcut, and
  double-click/context-menu copies are not yet represented by the neutral API;
  preserve or deliberately redesign these interactions in a follow-up.
- Validation: full `cmake --build build --config Debug --target KimPeanutEngine
  -- /m:1` passed after fixing one explicit `std::function<void()>` template
  argument in Terrain's panel registration. `LogEditor`, SignalLab, Terrain,
  Live2D, TTS, Audio, Editor, and the executable compiled. Visible review and
  the final static/placement scans remain pending.

- Task: review Editor for reuse and decoupling, then make a reconstruction plan.
- Scope: documentation only; source implementation is not authorized by this task.
- Baseline: HEAD `90f6597668c74774fcbdd3e11ed57cd0cf9545d3` with pre-existing
  uncommitted Settings/Editor work. Settings placement evolved during reading;
  the plan records the observed model/view split without treating interim paths
  as a new source failure or undoing that work.
- Canonical artifacts: [ED6 design](../../docs/editor/.plan/ED6.md),
  [review](../../docs/editor/.review/ED6.md),
  [scope](../specs/editor-ui-reconstruction.md).

## Investigation and placement

Operation and acceptance: identify why UI reuse imports Editor/feature policy;
record a staged, buildable reconstruction with explicit owner/lifetime/target
boundaries and concrete validation, without changing source.

Candidates inspected: EditorUI/backend lifecycle and panel factories;
EditorWindowComponent content/window behavior; tool-row ownership, hidden pumps,
headless layout/placement and persistence; feature models and current Settings
source grouping; host callbacks and Engine's concrete Editor ownership;
Audio/TTS, Terrain, Signal and Live2D consumers; global extension registrations.

Placement decision: preserve proven low-level seams and models; extract generic
UI/presentation, reuse workspace machinery, move feature tool ownership to
domain-adjacent editor targets, and keep SceneEditor assembly in Editor. Preserve
runtime Scene3D ownership and replace concrete UI dependency at application
integration. Directory-only moves and a new generic service bag were rejected.

Reference discovery used read-only GitHub repository search for SakuraEngine,
Piccolo and Walnut, followed by pinned actual build/header/implementation source.
One initial Piccolo header path was absent; repository directory inspection
located `engine/source/editor/include`. Full source/revision mapping and the
adopt/modify/reject rationale live in ED6. No reference source was imported.

## Documentation delivered

- Added proposed ED6 stage design, seven open review findings and execution scope.
- Added ED6 roadmap acceptance and architecture navigation, with a short status
  entry. Existing implementation/acceptance ledgers remain intact.
- Defined explicit content/update/close hosting, per-workspace contributions,
  feature targets, layout compatibility, neutral Runtime lifecycle integration,
  loading/closing preservation and independent link tests.

## Validation

- Before-edit placement evidence:
  `tools/agent/placement.ps1 -Paths @('docs/editor','docs/status.md') -Pattern 'EditorUI|EditorWorkspace|ED6'`
  passed structural checks. This does not establish semantic source placement.
- Final placement evidence repeated the same command and passed structural
  checks. Semantic review confirmed that the plan retains separate domain,
  workspace, presentation and native GPU owners.
- `git diff --check` passed. Markdown fences are balanced in all seven changed
  documents. Local file/anchor validation covered 485 links across those files;
  it found two unrelated existing status-ledger anchors, `ap11-product-verification`
  and `ap12-gpu-native-texture-compression`, already referenced in HEAD. They were
  left unchanged. A scoped rerun for the plan, review, spec, journal, architecture
  and roadmap passed; new ED6 status links were also checked successfully.
- Skipped: CMake configure/build, tests, engine launches, screenshots and timing
  runs. This is Level 0 documentation planning; no runtime result is claimed.
- No source implementation completed; all ED6.0–ED6.7 implementation acceptance
  remains open. Final host attachment API and migration schema version are bounded
  implementation decisions recorded in the design.

## Follow-up scope clarification — 2026-11-10

The user raised GUI-library replacement and structured UI information that an
agent can understand and translate back. They then requested reconstruction
first, with a new design afterward. Recorded the requirement in the Editor
README, ED6 scope and deferred roadmap. No UI schema or reverse-translation
semantics were selected, and no source implementation was started by this
clarification. The current ImGui reconstruction remains the first workstream.

## Log editor placement follow-up

- Moved log filtering, search, selection and ImGui row rendering into
  `engine/runtime/core/log/editor/` and the separate `LogEditor` target. The
  existing Editor `EditorLogComponent` remains as a thin window host because
  `EditorWindowComponent` owns window lifecycle and several tools embed this
  host. `Log` itself stays UI-free.
- `cmake --build build --config Debug --target LogEditor EditorUILib AudioPlayerMode -- /m:1`
  passed. Scoped `tools/agent/placement.ps1` and `git diff --check` passed.
- This is a narrow implementation of the planned feature-adjacent log view;
  it does not close the broader ED6.0–ED6.7 acceptance.

## ED6.1 content and host lifecycle follow-up — 2026-11-10

- The user authorized execution of ED6.1 after asking whether ED6.0 was complete.
  It was not: the source/target consumer map exists, but saved-layout and command
  inventories, Debug correctness baselines, and matched RelWithDebInfo UI
  build/submit timings were not captured. No pre-change timing baseline could be
  reconstructed after the fact. ED6.0 remains open; ED6.1 was executed as the
  specifically authorized stage.
- Operation and acceptance: share draw/update/close behavior across tab and
  floating hosts; hidden content continues upkeep; close may accept, defer, or
  cancel; generic UI components no longer own scene layout slots.
- Candidates inspected: existing `EditorWindowComponent` draw/visibility path,
  `EditorToolRowComponent` dock/floating traversal and `PanelPump` callbacks,
  `EditorUI` layout traversal, Console and Debug Viewer upkeep, and Settings'
  draft-close modal. Chosen owners: `EditorPanelContent` defines the UI-free
  lifecycle; `EditorWindowVisibility` owns deferred visibility state;
  `EditorLayoutParticipant` carries the optional layout capability;
  window/tool-row adapters remain responsible for ImGui host scopes; each
  feature retains its own update, draft and close policy. No domain algorithm or
  GPU lifetime moved.
- Implemented the content lifecycle and layout participant interfaces, routed
  hidden upkeep and every panel close affordance through the common lifecycle,
  retained `EditorWindowComponent` as the compatibility host, and adapted
  Settings' Apply/Discard/Cancel flow to deferred close decisions. ImGui
  Begin/End scopes stay paired, with contents/chrome skipped when Begin returns
  false. Added ImGui-free lifecycle tests and headless tab/floating host tests.
- Validation: sequential Debug build of `EditorUILib`,
  `EditorToolRowModelUnitTest`, `EditorUILifecycleTest`,
  `EditorSettingsModelUnitTest`, and `KimPeanutEngine` passed. Direct test runs
  passed 51/51, 6/6, and 5/5 tests respectively. `ctest` reports no registered
  tests for these targets, so the built GoogleTest executables were run directly.
  Rebuilt Editor was launched visibly with Vulkan; the Scene Editor rendered the
  Sponza scene with its workspace panels. The Settings draft-close modal was
  also inspected in the preceding visible check; the temporary draft was
  reverted, not saved.
- The ImGui scope checker reports two false positives in touched code: counted
  `PushStyleColor`/`PopStyleColor(pushed)` is balanced, and
  `BeginPopupModal` correctly pairs with ImGui's `EndPopup`. Other touched UI
  files passed its checks. Manual inspection confirmed both scopes.
- Skipped: matched RelWithDebInfo UI timing baseline (unavailable), Debug
  before/after correctness comparison, standalone-host and OpenGL visible checks,
  shutdown stress, and final ED6.0 inventories. ED6.2–ED6.7 remain unstarted;
  ED5 and SET3 acceptance remain independent.

## ED6.0 baseline freeze follow-up — 2026-11-10

The user revised the design contract: feature and workspace views in every
module must use Editor's general toolkit-neutral UI API; only the Editor ImGui
backend may include ImGui headers or make ImGui calls. This supersedes any
reading of the prior plan that allowed direct ImGui in feature views. The
[Editor README](../../engine/editor/README.md) is the responsibility authority.

- Froze ED6.0 at HEAD `90f6597668c74774fcbdd3e11ed57cd0cf9545d3` plus the dirty
  working tree, which includes Settings/Log placement work and ED6.1 source.
  This is the baseline before ED6.2, not a pristine pre-ED6.1 snapshot.
- Added [ED6.0 review](../../docs/editor/.review/ED6.0.md): it records the
  current build/include/toolkit dependency graph; all known external feature
  targets with direct ImGui links; current EditorUI tool/component owners and
  borrow lifetimes; panel IDs, close defaults, command names, extension callers,
  scene saved-layout schema and standalone visual references. It explicitly
  labels direct toolkit calls as legacy migration work. No source was moved.
- Reconfirmed current target edges: RuntimeLib -> EditorLib -> EditorUILib ->
  RuntimeLib; `LogEditor`, AudioPlayerMode, SignalLabMode, TerrainEditor,
  TtsEditorMode and Live2DModule also currently compile/link ImGui directly.
  Settings editor sources live under `runtime/settings/editor` but are still
  compiled into EditorUILib. Feature runtime libraries Log and RuntimeSettings
  themselves remain ImGui-free. The global extension registry has no production
  registration caller.
- Scene layout evidence: `save/editor_layout.json` is version 3, panel-keyed
  placements plus named split fractions; readers accept versions 1–3 and
  convert v2. Scene panel commands are `editor.panel.list`,
  `editor.panel.show(id)` and `editor.panel.focus(id)`. Scene stable panel IDs
  are recorded in the review. Standalone layouts are application-composed and
  have no second shared editor layout path. Existing archive references include
  Audio Player, Signal Lab, TTS and Terrain; no Live2D shell screenshot was
  found. The current scene was inspected in the visible app. This is a frozen
  owner/layout baseline, not cross-host visual acceptance.
- Debug build passed for EditorUILib, KimPeanutEngine and nine Editor model/UI
  test targets. Direct runs passed 150/150 tests: lifecycle 6, loading 4, Actor
  model 3, tool-row/content 51, commands 4, Settings model 5, layout 47, Asset
  browser 22, Asset reference 8. The first layout test run failed because the
  sandbox redirected Windows Temp outside the writable workspace; rerunning
  with `TEMP`/`TMP=D:\C++Project\KimPeanutEngine\build\test-temp` passed all
  47. No Debug Vulkan-validation runtime result is claimed.
- Rebuilt the full application in RelWithDebInfo, then launched it visibly. Build
  succeeded with MSVC LNK4098 default-library conflict warning. Conditions:
  commit/worktree above, Vulkan, default current Sponza scene/camera (exact
  startup fixture path and camera transform not independently queried), window
  1283x751 and viewport about 729x420, path tracing active, texture residency
  complete after 25 seconds warm-up. Validation was not enabled for this timing
  configuration.
- Sampled the visible profiler five times across about 15 seconds. A clearly
  readable frame was **ImGui total 1.31 ms: build 0.62 ms, submit 0.70 ms**.
  These are the existing `EditorUI` CPU scopes (build begins before pre-draw and
  submit includes renderer work); they are not isolated widget cost or GPU
  execution. The sample set was not exported, so no mean or percentile is
  claimed. No settings or workspace geometry were changed for the sample.
- The RelWithDebInfo read completes the requested baseline sample; it does not
  replace matched final ED6 performance acceptance. ED6.0 is now marked frozen
  in the roadmap. ED6.2–ED6.7, fresh every-host visual checks, Debug Vulkan
  validation and final matched measurements remain follow-up. ED5 and SET3
  acceptance are independent.

## Toolkit boundary clarification — 2026-11-10

- The user requires feature modules to use Editor-provided general UI during
  ED6, with direct ImGui usage confined to the backend. This supersedes the
  earlier plan allowance for direct toolkit drawing in feature views.
- Updated README, architecture/agent guides, roadmap, ED6 and spec invariants:
  neutral public UI types/API and private toolkit dependency in ED6.2; migrate
  all feature/workspace calls, including custom canvases/input/images, by ED6.5;
  enforce the boundary at ED6.7. Existing ED6.1 lifecycle acceptance is preserved.
- Another production GUI backend and structured agent UI remain later work.
  This documentation clarification does not extend ED6.1-only source scope or
  claim the toolkit boundary is already implemented.
- Scoped validation passed 57 local file links/anchors and Markdown fences in
  six documents; final placement checks passed. No C++/CMake edits, builds,
  runtime runs or screenshots were performed for this clarification.

## Feature-view migration — 2026-11-11

- TTS's `TtsEditorEditor` now submits a neutral host container, feature-owned
  `UiTheme`, controls, virtualized dialog rows, audio transport actions, and a
  general `UiPlot` waveform. Import uses Editor Asset's UTF-8 file dialog.
  TTS state and all generate/playback/settings/file actions remain in its
  controller and module editor. `TtsEditorMode` no longer links `KP::ImGui` or
  `ImageIO` directly.
- Added a neutral multiline bit to text-control descriptors for speech text
  and reference text. The ImGui implementation selects `InputTextMultiline`;
  no ImGui type crosses the API. Virtualized list scopes are now composable
  inside table cells, required for the TTS two-column workspace.
- General UI owns the text entry and layout capability; TTS owns its cyan theme,
  waveform palette, TTS controls and action policy. The Editor Asset dialog
  service is reused; no new file-dialog owner was introduced. Runtime has no
  new Editor dependency.
- `TtsEditorMode` Debug build passed. Placement audit passed and TTS editor
  source/CMake scans found no direct ImGui declarations or `KP::ImGui` link.
  Visual interaction review was not performed. The previous focus-on-new-speech
  behavior and Ctrl+Enter shortcut are not represented in the current neutral
  API; the decorative avatar image was removed because there is no neutral
  raster-image contract yet. These are follow-up gaps to assess during visible
  review. A first compile attempt hit an MSVC internal compiler error on a
  temporary string-view descriptor; using named descriptor storage cleared it.
- Terrain's CPU heightmap comparison now uses the neutral Canvas and Table
  APIs. Elevation palette and comparison policy remain in Terrain; sampling is
  capped at 128x128 per map to bound submitted primitives for large fields.
  `TerrainEditor` still owns its snapshots, and passes the active UiContext to
  `HeightmapDebugView`; the main viewport and controls remain in ImGui pending
  neutral render-target and input contracts. `TerrainEditor` Debug build passed,
  and the heightmap view source has no ImGui symbols. No visible terrain review
  was performed.

## ED6.5 unreferenced wrapper cleanup — placement record — 2026-11-11

- Operation and acceptance: remove the reviewed, unreferenced legacy UI wrappers from the general Editor build and tree without changing any live host behavior; CMake and the ED6.2 compatibility inventory must no longer name them.
- Existing candidates inspected: `EditorButtonComponent`, `EditorListboxComponent`, `EditorPlotComponent`, `EditorTextComponent`/`EditorDynamicTextComponent`, `EditorTooltipComponent`, and `EditorContainerComponent` appear only in their own implementation/header group (Tooltip also wraps Text). Their `.cpp` files are listed in `engine/editor/ui/component/CMakeLists.txt`, but no caller or test uses them. `editor_camera_component.h`, `editor_scene_component.h`, `editor_drag_component.h`, and `editor_slider_component.h` have no in-tree include or symbol consumers. The review identifies `UiContext` controls/Plot and the current tool-row host as their existing live mechanisms.
- Placement: delete these legacy wrappers and remove their six `EditorUILib` source entries. Do not extract a replacement owner: no current consumer needs one, and the neutral API/workspace already provide the used behavior.
- Consumers, ownership/lifetime, validation: no runtime or tool lifecycle state is moved. Update dead-code history, the historical component overview, and the measured ED6.2 ImGui allowlist; then audit CMake/search/placement and compile the owning `EditorUILib` target. No CTest expansion is warranted for removing unreferenced source.

## ED6.5 unreferenced wrapper cleanup — result — 2026-11-11

- Removed six unreferenced compiled wrapper groups (`button`, `container`, `listbox`, `plot`, `text`, `tooltip`) and their CMake entries, plus four unreferenced headers (`camera`, `scene`, `drag`, `slider`). Tooltip/Text were removed together because Tooltip privately constructed Text. No caller or test behavior moved; the live equivalents remain the neutral controls/Plot and the workspace host.
- Updated the ED6.5 review to mark these candidate dispositions complete while retaining the review-time 22-source/31-header inventory as a baseline. Updated the module overview, dead-code ledger and ED6.2 measured ImGui allowlist; current Editor source scan has 39 matches and all 39 are represented in the allowlist.
- Repository code searches show no remaining removed type or filename references. Post-edit placement structural checks passed for the live workspace host and neutral control owner. `git diff --check` passed with only repository line-ending notices.
- Validation: serial Debug `cmake --build build --config Debug --target EditorUILib -- /m:1` passed (exit 0) after CMake regeneration. No tests were run because this cleanup removes unreferenced code and no test was requested. ED6.5 remains in progress; viewport capture/input and Audio Player pane recovery, workspace extraction, Console/diagnostic migration, and final host acceptance remain open.
# RenderEditor placement record — 2026-11-11

Operation and acceptance: Move the GPU Profiler and Debug Viewer presentation/lifecycle out of `engine/editor/ui/component` into `engine/runtime/render/editor/`, built as an independent `RenderEditor` target. The profiler consumes Render's published performance snapshot and receives frame-loop plus UI build/submit measurements from SceneEditor. The Debug Viewer continues requesting its selected diagnostic view only while visible and releases the request when hidden or destroyed.

Existing candidates inspected: `EditorGpuProfilerComponent` currently reads `RenderSystem::GetPublishedMetrics()`, `Engine::GetFrameLoopMetrics()`, and `EditorUI::GetLastImGui*()` while directly drawing ImGui; `EditorDebugViewerComponent` owns visibility demand via `UpdateContent(bool)` and destructor release, and directly obtains the Render target. `EditorToolRowComponent::RenderImpl` calls `Update(IsDrawnThisFrame(index))` for every panel, including hidden tabs, and invokes `RenderContent(UiContext&)` when drawing. Existing UI contracts in `engine/editor/ui/api/editor_ui/` provide tables, controls, text, canvas image primitives, and clipboard. Render already owns immutable published metrics, debug-view demand aggregation and the render target; SceneEditor owns the frame loop and UI timing sources.

Placement and reason: Introduce focused RenderEditor view classes and an independent target that links Render plus the general `UIWidgets` contract. Keep the tool-row window wrapper in Editor as composition/hosting glue; it delegates draw/update to RenderEditor and supplies the editor renderer's render-target image registration callback. This avoids Runtime-to-Editor/ImGui dependencies, keeps Render as the sole owner of snapshots and GPU diagnostic resources, and lets SceneEditor inject measurements without a reverse dependency.

Consumers, ownership/lifetime, and validation: `EditorUI` owns RenderEditor view objects through dock-panel wrappers; each view borrows `RenderSystem`, whose lifetime already exceeds the editor workspace. Debug-view demand remains keyed to `DebugViewConsumer::EditorDebugViewer`, is updated by panel visibility, and is cleared on destruction. RenderTargetView remains borrowed for the duration of drawing and is registered with the editor image renderer each frame. Build the focused `RenderEditor` and editor targets; do not run CTest under the current workflow request.

## RenderEditor migration result — 2026-11-11

- Added independent `RenderEditor` with `GpuProfilerView` and `DebugViewerView`; the old ImGui-backed profiler/debug components and their EditorUILib source entries were removed. The views draw through `UIWidgets`; `EditorUI` keeps only dock-window wrappers and injects the SceneEditor timing snapshot and target-image registration callback.
- The profiler reads `RenderSystem::GetPublishedMetrics()` each draw. Frame-loop and UI build/submit durations arrive as values from Editor composition; no Runtime or RenderEditor code reaches back into Engine/Editor. Debug view selection still requests `EditorDebugViewer` demand while active; hidden updates and destruction clear that demand.
- Reused existing `RenderProfileSnapshot`, published Render metrics, debug-demand aggregation, UiContext table/control/canvas/clipboard contracts and `EditorUI::RegisterRenderTargetImage`. Render continues owning all telemetry and diagnostic targets. No new mutable cross-module state or GPU ownership was added.
- Validation: `cmake --build build --config Debug --target RenderEditor KimPeanutEngine -- /m:1` passed after CMake regeneration, including a second pass after retaining the profiler's advanced Render counters. `dumpbin /symbols RenderEditor.lib` found no ImGui symbols. No tests or runtime visual checks were run; verify visible Debug Viewer selection and demand release in the UI.

## Canvas image submission / host migration — placement record — 2026-11-11

- Operation and acceptance: make Canvas image primitives render through the existing ImDrawList without registering another ImGui item, and snapshot Canvas hover/press/drag/focus/visibility before submitting any primitives. Then extract the common production UI host lifecycle from EditorUI so standalone module tools can use it without constructing the SceneEditor aggregate; keep the current docked-workspace behavior and saved IDs/layout intact while moving the workspace owner to `EditorWorkspace`.
- Existing candidates inspected: `ImGuiInlineBackend::Canvas` creates one InvisibleButton, invokes EditorUI's image callback, then samples `IsItem*`; the callback calls `IEditorImguiRenderer::DrawSceneImage`, which calls `ImGui::Image` in both GL and Vulkan backends. Terrain consumes Canvas wheel and secondary-drag results. `EditorUI` currently owns context/font, WSI, renderer, UiSession, image registration, frame lifecycle and rendering; Audio, TTS, Signal Lab, Terrain and Live2D all construct `EditorUI` only to call `InitializeUiViewer`/`Render`/`Close`. `EditorDockedWorkspace` is Audio-owned in practice but uses EditorToolRowComponent and EditorProfileBarComponent. These are plausible boundaries, but workspace APIs and module consumers must remain value/callback based.
- Placement and reason: keep Canvas item and draw-list conversion in `UIBackendImGui` plus its private renderer adapter; do not let RenderTarget image submission change Canvas identity or layout. Add a focused EditorUiHost to own native presentation/session lifecycle and image registration, while EditorUI composes SceneEditor tools on that host. Promote the existing generic dock model/host to EditorWorkspace rather than creating a second panel protocol; first migrate the Audio workspace wrapper and SceneEditor panel host without changing stable IDs or layout decoding.
- Consumers, ownership/lifetime, and validation: the production host owns ImGui context, WSI, renderer and UiSession; view callbacks are borrowed only during `RenderFrame`, and image sources are frame-local. SceneEditor retains its own domain objects and injects frame callbacks/timing. Debug Vulkan/OpenGL builds plus the production Canvas image/input smoke must verify hovered, wheel and secondary-drag results with present/missing image sources; no full CTest run unless a targeted relevant test is added/requested.

## Canvas draw-list submission — implementation result — 2026-11-11

- Canvas now snapshots bounds, hover, click, active/focus/visibility, drag, wheel and normalized pointer state immediately after the Canvas `InvisibleButton`. Image primitives are routed through a private `ImDrawList*` callback, so rendering cannot register another ImGui layout/input item.
- GL uses `AddImage` with the previous vertically flipped UVs and preserves the existing sRGB marker callbacks. Vulkan uses `AddImage` with its prior UV orientation. Render-target ownership and per-frame image registration remain with the current Editor presentation owner.
- Reused `UiCanvasResult`, the existing renderer adapters and ImGui draw lists. The adapter interface gained one primitive submission operation; no new texture owner or long-lived image state was introduced.
- Validation: serial Debug `cmake --build build --config Debug --target EditorUILifecycleTest -- /m:1` passed; `git diff --check` and the placement structural checks passed. A targeted headless hover/wheel test was attempted but could not establish hovered-window state in the test WSI; that temporary test was removed rather than retaining an invalid regression. Terrain interaction with visible/missing images remains unverified. Full production UI host and EditorWorkspace extraction remain open.

## EditorUiHost extraction and viewer migration — result — 2026-11-11

- Added `EditorUiHostLib`, separating presentation lifecycle and ImGui adapter dependencies from the SceneEditor aggregate. `EditorUiHost` owns context, theme font, WSI/renderer, `UiSession`, image bindings, frame dispatch and timing. `EditorUI` composes it and retains SceneEditor-specific models, panels, selection routing, settings and screenshot integration.
- Audio Player, TTS, Signal Lab, Terrain and Live2D now create/render/close `EditorUiHost` directly. Their source no longer includes `EditorUI` or constructs `EditorUIInitInfo`; Live2D passes its custom background color through the host. Their static libraries have no ImGui symbols by `dumpbin /symbols`.
- Split `EditorSettingsConfigLib` so both the standalone host and SceneEditor can read persisted palette settings without linking the host back to `EditorUILib`. Fixed a misspelled WSI header include guard exposed when the interface was included through both the host and GLFW adapter.
- Reused the existing renderer/WSI interfaces, `UiSession`, theme implementation and image registration contract. The host owns those resources and clears borrowed render targets each frame; no new GPU ownership was added. Audio still depends on `EditorUILib` because `EditorDockedWorkspace` remains backed by `EditorToolRowComponent` and `EditorProfileBarComponent`.
- Moved `EditorUiSmoke` and Settings-fields smoke onto the same host API. Vulkan and OpenGL variants compile; the GUI smoke was not launched.
- Validation: serial Debug build passed for `KimPeanutEngine`, `EditorUILifecycleTest`, `AudioPlayerEditorUI`, `TtsEditorMode`, `SignalLabMode`, `TerrainEditor`, `Live2DModule`, `EditorUiSmoke`, `EditorUiSmokeOpenGL`, `EditorSettingsFieldsSmoke` and `EditorSettingsFieldsSmokeOpenGL`. Three targeted presentation/image lifecycle tests passed. All five module libraries reported no ImGui symbols. Placement structural checks passed. No visual run was performed; Canvas pointer behavior remains open. Next: migrate dock, split, floating and layout behavior into `EditorWorkspace` and remove Audio's remaining `EditorUILib` dependency.

## EditorWorkspace extraction — placement record — 2026-11-11

- Operation and acceptance: move workspace topology, splitter ratios, panel placement/visibility, dock tab selection, floating hosts and recovery actions into an `EditorWorkspace` owner independent of `EditorUILib`; migrate Audio Player to it and remove Audio's link to the legacy component library while preserving its seven panel IDs, three-column/two-row starting layout and per-panel content/theme ownership.
- Existing candidates inspected: `EditorLayoutModel` contains the headless binary split-tree arithmetic but hardcodes SceneEditor region/splitter enums and presets; `EditorToolRowModel` owns tab/visibility/lock/floating placement but also names scene slots; `EditorToolRowComponent` and `EditorSplitterHandles` are ImGui component hosts in `EditorUILib`; `EditorDockedWorkspace` composes those and `EditorProfileBarComponent`; `UiContext` exposes neutral windows, tabs, controls and Canvas while `EditorUiHostLib` owns presentation lifecycle.
- Placement and reason: add a focused EditorWorkspace model/host that uses caller-supplied region and split IDs/presets and only the neutral `UiContext`; keep SceneEditor's existing layout adapter/settings reader during compatibility migration. Audio supplies its own three-column/two-row preset and panel definitions; feature callbacks keep owning content and styles. Do not add file-dialog or audio-specific policy to the workspace.
- Consumers, ownership/lifetime, and validation: AudioPlayerEditor owns the workspace instance and registers stable panel callbacks; workspace owns layout and panel lifecycle values, not content or renderer resources. Build the new workspace and Audio targets serially, verify AudioPlayerEditorUI has no EditorUILib link or ImGui symbols, run placement and diff checks; no GUI launch or CTest is requested in this step.

## EditorWorkspace extraction — implementation result — 2026-11-11

- Added `EditorWorkspaceModelLib` with caller-supplied string region/split IDs, a validated split tree, resolved bounds, per-instance panel placement/visibility and split fractions. Added `EditorWorkspaceLib`, which renders region containers, tab groups and floating windows through neutral `UiContext`; close/reopen, float and dock-to-region actions live in the workspace.
- Audio supplies its own three-column/two-row preset and keeps all seven stable panel IDs, content callbacks and `AudioPlayerTheme` scopes in `module/audio/editor`. Removed `EditorDockedWorkspace` and its component source registration. `AudioPlayerEditorUI` links `EditorWorkspaceLib` and `EditorUiHostLib`, not `EditorUILib`.
- Reused the existing neutral Window, TabBar, TabItem, Button, Combo and Slider API. The workspace owns geometry and placement values; module content and presentation renderer lifetimes remain with Audio and `EditorUiHost`. No new Runtime dependency was added.
- Validation: serial Debug build passed for `EditorWorkspaceLib` and `AudioPlayerEditorUI`. Audio editor source contains no ImGui references and its CMake target has no `EditorUILib` link. Full symbol-name scanning sees the historical `IEditorImguiRenderer` type inside the host initialization API, but no Dear ImGui backend symbols are present in the Audio library. `git diff --check` and post-edit placement checks remain to run. No tests or visible GUI checks were requested. Interactive splitter dragging, saved workspace state, floating-bound occlusion, visible appearance and Audio's old profile footer remain unverified/follow-up items.

## ED6.3 interaction review fixes — 2026-11-11

- Read the reproduced cases in `docs/editor/.review/ED6.3.md` and changed the workspace and legacy host paths. Audio queues Float/Move/Close/Show/Select actions until its frame traversal is complete; all standard workspace IDs now carry an instance namespace. It has direct neutral Canvas tab dragging with a release preview and Canvas splitter handles; cancellation restores the starting ratio. The tree resolver uses descendant minimum extents and the controls share its effective range. The private ImGui adapter gives each native window a stable ID independent of its visible caption and exposes captured bounds and pointer state through the neutral API.
- SceneEditor now subtracts floating panels, Settings and Asset Reference from splitter drawing/hit testing and from one shared dock preview/drop resolver. Splitter capture chooses one seam; Escape, focus loss, invalid pointer, missing release or seam disappearance restore/cancel the active drag. Dock gestures start only on an explicit tab press and cancel on the same invalidation paths. Layout persistence retains a dirty latch when writing fails.
- Reused the existing neutral Canvas/backend, `EditorWorkspaceModel` geometry owner, SceneEditor layout model and layout serializer. Kept feature panel drawing with Audio, rendering backend behavior inside the private ImGui adapter, and legacy SceneEditor placement compatibility in `EditorUILib`. No new module dependency was introduced.
- Validation: Debug build succeeded serially for `EditorUILib`, `EditorWorkspaceLib` and `AudioPlayerEditorUI`; post-edit placement structural checks and `git diff --check` passed. No test suite or headless interaction repro was run. A visible Debug Vulkan launch confirmed Audio Player starts, and exposed that the Workspace utility window was submitted even after its close flag became false. It is now submitted only while `layout_open_` is true and the target libraries rebuilt; the visual fix was not relaunched. Audio versioned workspace persistence and float-window recovery/clamping remain open.
