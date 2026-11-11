# Editor reconstruction review and planning — 2026-11-10

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
