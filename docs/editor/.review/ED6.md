# ED6 — Editor reuse and dependency review

- Date: 2026-11-10
- Status: reviewed for reconstruction planning; findings open
- Design: [ED6](../.plan/ED6.md)
- Baseline: HEAD `90f6597668c74774fcbdd3e11ed57cd0cf9545d3` plus the existing
  dirty working tree, including the in-progress Settings UI placement changes.
- Scope: source/build architecture, component contracts, real standalone
  consumers and presentation lifecycle. No source edits, builds, runtime tests,
  or visual/performance verification were performed for this review task.

## Findings

### F1 — High: generic presentation and Scene Editor composition share one owner

Open. `EditorUIInitInfo` in
[`editor_ui.h`](../../../engine/editor/ui/editor_ui.h) carries Engine,
RenderSystem, LogSystem, Settings, Gameplay, Asset and camera/selection services.
The same class owns renderer/WSI lifetime, feature models, scene panel factories,
loading UI and layout persistence. `InitializeViewer()` bypasses scene
construction for standalone tools, while retaining the same type/target.

Impact: a reusable presentation consumer depends on the scene tool shell;
feature additions grow generic initialization, lifecycle and teardown paths.
Remedy: ED6.2 extracts presentation; ED6.3/ED6.5 separate workspace hosting from
SceneEditor assembly. Verify a real UI-only link and two real workspace presets.

### F2 — High: the build graph prevents an independent UI/runtime boundary

Open. [`editor/CMakeLists.txt`](../../../engine/editor/CMakeLists.txt) compiles
generic controls, platform integration and concrete feature tools into
EditorUILib and links RuntimeLib. [`runtime/CMakeLists.txt`](../../../engine/runtime/CMakeLists.txt)
links EditorLib, and [`engine.cpp`](../../../engine/runtime/engine.cpp) includes
`editor/editor.h` and constructs concrete Editor in `Engine::Engine()`.

Impact: a directory move does not create an independently linkable UI module;
the known Runtime/Editor static-library cycle continues to couple consumers.
Remedy: extract actual targets and remove the concrete dependency through
neutral host lifecycle integration in ED6.6. Verify transitive target edges and
runtime-only linking; a successful static archive alone is insufficient.

### F3 — High: window inheritance does not guarantee the same panel lifecycle

Open. [`EditorWindowComponent`](../../../engine/editor/ui/component/editor_window_component.h)
combines top-level window hosting and content drawing. The dock host calls
`RenderContent()` directly in
[`editor_tool_row_component.cpp`](../../../engine/editor/ui/component/editor_tool_row_component.cpp),
while standalone windows use `Render()`. The separate hidden `PanelPump`
contract already demonstrates upkeep that cannot depend on visibility.

Impact: a tool placing model refresh, close interception or pending-result work
in `Render()` is not automatically reusable in a tab. This is an architectural
hazard; the review does not assert that every current panel loses those behaviors.
Remedy: explicit update/content/close operations with host-owned window state;
test the same tool in both hosts, including hidden and deferred-close states.

### F4 — Medium: generic component and layout contracts carry scene policy

Open. [`EditorUIComponent::GetLayoutSlot`](../../../engine/editor/ui/component/editor_ui_component.h)
depends on [`EditorLayoutSlot`](../../../engine/editor/ui/component/editor_layout_model.h),
whose identities include WorldOutliner, ActorInspector, CameraSettings and
GpuProfiler. The headless tool-row model also declares concrete tool IDs.

Impact: unrelated workspaces must understand scene names and defaults to reuse
the layout machinery; a primitive's public contract depends on application policy.
Remedy: keep placement on workspace attachments, provide region descriptors
and presets, and move scene/tool IDs to their owners. Preserve ED2/ED3 algorithms
and legacy serialized keys; dynamic docking is not required for this split.

### F5 — Medium: feature ownership remains broader than source placement

Open. The Settings model/view is moving into `runtime/settings/editor`, but its
[`CMakeLists.txt`](../../../engine/runtime/settings/editor/CMakeLists.txt)
adds sources to EditorUILib, and EditorUI still constructs/owns concrete
Settings state. Log is already reused by several real hosts, but its viewer
belongs to the same broad Editor target. AudioPlayerMode even names an Editor
source path in its own build list.

Impact: feature UI cannot be linked/omitted according to its real consumers;
shared source location and shared target ownership disagree.
Remedy: separate feature editor targets beside Log, Settings, Command, Audio,
Asset and Gameplay. Compose them explicitly. Preserve current Settings work and
verify the chosen state again before moving files. ED6.4/ED6.5 own migration.

### F6 — Medium: presentation initialization and configuration imply feature policy

Open. `EditorUI::InitializePresentation()` reads Editor settings and builds a
loading tree, including for `InitializeViewer()` callers that immediately
discard that tree. `RenderActiveTree()` reaches the prepared Render asset
catalog for bloom shaders. The
[`renderer interface`](../../../engine/editor/platform/editor_imgui_renderer.h)
uses LogColor for its general background color. The
[`Log viewer`](../../../engine/editor/log/editor_log_component.cpp) silently
falls back to the process-global Logger when its supplied LogSystem is null.

Impact: tool applications inherit configuration/data-source policy and broader
service dependencies from an operation expected to initialize presentation.
Remedy: explicit theme/configuration and narrow resource/log sources, supplied
at composition. Preserve loading-safe assets, snapshot synchronization and
Graphics-owned GPU lifetime; ED6.2/ED6.4 cover the separation.

### F7 — Medium: the extension mechanism does not describe a workspace tool

Open. [`EditorExtensionRegistry`](../../../engine/editor/ui/editor_extension_registry.h)
is global and supplies RenderSystem/renderer to every component factory.
`BuildRegisteredWorkspaceExtensions()` lacks panel identity/menu/placement data
and keeps extensions as top-level components. The search found the registration
declaration/definition but no production registration caller.

Impact: it does not currently replace the shell's concrete panel factories and
cannot by itself establish per-workspace model/callback ownership.
Remedy: explicit per-workspace contributions over the existing host operations,
with stable identity, transactional registration, close/update contracts and
callback teardown. Recheck callers before retiring the old API in ED6.3.

## Preserved strengths and scope limits

Retain the existing WSI/renderer seams, common Graphics presentation bridge,
copied frame data, headless layout rules, stable visibility/IDs, and copied
domain snapshots/deferred edits. A complete widget framework rewrite is not
supported by these findings. Feature co-location does not require UI ownership
inside a runtime service object; the feature's editor session owns UI state.

Reference source comparison and rejected alternatives are recorded in
[ED6](../.plan/ED6.md#reference-study-and-decisions). This review is static
architecture evidence, not proof of visual regressions, runtime failure,
performance problems or GPU lifetime correctness.

## Validation performed

- Read target wiring, component/host implementations and actual Audio/TTS,
  Terrain, Signal and Live2D consumers; inspected current Settings move.
- Searched extension registration, concrete Editor ownership, scene-layout
  dependencies and panel content/pump call paths.
- Inspected pinned Sakura, Piccolo and Walnut source via read-only GitHub tools.
- Documentation link/diff checks are recorded in the planning journal.
- Builds/runtime checks skipped: review and plan only; no executable changes.
