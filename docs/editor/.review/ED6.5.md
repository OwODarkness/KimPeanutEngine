# ED6.5 — `editor/ui/component` migration review

- Date: 2026-11-11
- Baseline: `70ff69e05666c93f8d11115feb5b538057596ab8` plus the current dirty worktree, including the in-progress neutral feature views, Settings window move, UI smoke harness and removal of `editor_audio_component`.
- Scope at review baseline: all 22 `.cpp` files and 31 headers then present in `engine/editor/ui/component`, its source list, and direct owners, callers and tests. Findings are open unless marked otherwise.
- Stage: [ED6.5](../.plan/ED6.md#ed65--complete-feature-migration-and-sceneeditor-composition). Workspace ownership is an [ED6.3 prerequisite](../.plan/ED6.md#ed63--establish-explicit-workspace-composition). This review follows the [feature-view migration scope](../../../.spec/specs/editor-feature-ui-migration.md) and leaves the [ED6.0 baseline](ED6.0.md) intact.

### Execution update — 2026-11-11

The first cleanup batch removed the six unreferenced compiled wrapper groups and
four unreferenced declaration/template headers identified below, along with
their `EditorUILib` entries. The file inventory remains the review-time baseline;
16 implementations and 21 headers remained after that cleanup. The subsequent
RenderEditor migration moved two source/header pairs, leaving 14 implementations
and 19 headers. This follow-up also audits the remaining Editor targets and their
direct feature callers. ED6.5 acceptance remains open.

At review baseline, the inventory separated 10 removable file groups from live tools and reusable workspace behavior. `EditorUILib` then compiled all 22 implementation files; six belonged to wrappers with no live consumer. The remaining work is chiefly SceneEditor composition, workspace hosting and diagnostics. Audio Player still reaches the workspace host through `EditorDockedWorkspace` even though its panel bodies use `UiContext`. [Component source list](../../../engine/editor/ui/component/CMakeLists.txt), [Audio Player composition](../../../engine/module/audio/editor/audio_player_editor.cpp#L155).

## Findings

### P1 · Viewport capture can outlive panel visibility · Open

The viewport enters capture on a right click and releases it in its visible draw path or destructor. It is a child of a generic `EditorWindowComponent`. The host sends visibility updates to the panel wrapper, whose update does not propagate to children. Deactivating or closing the viewport without destroying it therefore skips the viewport's release path. Runtime also uses this capture flag to enable its scene input context, so the problem can affect game-side camera input as well as the cursor. This is a statically traced path. Reproduce with capture active, then switch the active dock member or close the viewport and inspect both capture states. [Viewport capture and teardown](../../../engine/editor/ui/component/editor_viewport_component.cpp#L62), [viewport construction](../../../engine/editor/ui/editor_ui.cpp#L695), [host update](../../../engine/editor/ui/component/editor_tool_row_component.cpp#L125), [window update and child drawing](../../../engine/editor/ui/component/editor_window_component.cpp#L114), [Runtime capture sink](../../../engine/runtime/runtime_global_context.cpp#L1435).

Make the SceneEditor viewport content own capture release on visibility loss and teardown. Keep the existing camera and selection sinks, gizmo priority and `BuildSceneRay`. Verify close, tab switch, invalid or zero-sized image, focus loss, reopening and workspace teardown. A centered letterboxed image must supply the same rectangle to image drawing, gizmo hit testing and normalized pick coordinates.

### P1 · Audio Player close controls can make panes unreachable · Open

The shared host offers tab close and floating-window close. SceneEditor supplies a View menu built from its registered entries. Audio Player's workspace exposes `AddPanel`, `ShowPanel` and `Render`, and all current `ShowPanel` calls target `audio_queue`. The current Audio Player composition supplies no reopen action for the other registered panes. Closing Library, Now Playing, Info or Spectrum leaves no equivalent route in that workspace. [Host close controls](../../../engine/editor/ui/component/editor_tool_row_component.cpp#L264), [tab context actions](../../../engine/editor/ui/component/editor_tool_row_component.cpp#L411), [SceneEditor View menu](../../../engine/editor/ui/editor_ui.cpp#L394), [workspace facade](../../../engine/editor/ui/component/editor_docked_workspace.h#L17), [Audio registrations](../../../engine/module/audio/editor/audio_player_editor.cpp#L155).

Make recoverability explicit in workspace composition. Bind reopen actions to registered IDs or expose close only for panels with a recovery route. Keep one visibility state for docked and floating presentations. Verify closing the last tab in a dock and reopening it in Audio Player and SceneEditor.

### P1 · The reusable workspace still owns scene IDs and native window behavior · Open

`EditorLayoutModel` contains scene region/splitter identities and several presets. `EditorToolRowModel` declares built-in SceneEditor panel IDs. Audio Player passes that same slot enum through `EditorDockedWorkspace`, which selects its three-column preset internally and constructs a concrete profile bar. `EditorToolRowComponent` combines model transitions, native window/tab drawing, drag previews and foreground drop targets. `EditorUI::BuildToolRow` supplies all SceneEditor tools and also owns placement load/save and command registration. A directory move alone would leave Audio Player dependent on the aggregate and require edits to the same host when adding scene tools. [Layout presets](../../../engine/editor/ui/component/editor_layout_model.cpp#L62), [scene panel IDs](../../../engine/editor/ui/component/editor_tool_row_model.h#L31), [Audio workspace implementation](../../../engine/editor/ui/component/editor_docked_workspace.cpp#L28), [SceneEditor assembly](../../../engine/editor/ui/editor_ui.cpp#L843), [Audio target link](../../../engine/module/audio/editor/CMakeLists.txt#L5).

Use the ED6.3 destination. Retain the headless split math, placement, stable IDs and visibility contracts. Supply region/panel definitions, presets and optional footer from each composition. Keep the legacy scene-layout reader and `editor.panel.*` identities through migration. Let the host own update, close and focus across docked and floating panels. Restrict native window chrome and pointer drawing to the backend. Audio Player is a current second consumer with a different preset. Its panel IDs and default arrangement provide a concrete independence check.

The splitter currently receives one optional occluder. `EditorUI` passes only Asset Reference bounds and draws Settings after the splitter. The splitter performs both foreground drawing and raw click hit tests. Drawing Settings later supplies no bounds to that hit test, and floating dock panels have no exclusion in this call. Treat this as an arbitration gap to verify over empty panel space and title bars. Resolve actual visible bounds once for hit testing and overlay drawing. [Scene frame order](../../../engine/editor/ui/editor_ui.cpp#L1313), [splitter hit test](../../../engine/editor/ui/component/editor_splitter_handles.cpp#L95).

### P2 · Console belongs with Command and needs a text-edit contract · Open

The Console owns history, completion and suggestion text, CommandRegistry submission, a weak-state completion queue and input subscription. It pumps output through `UpdateContent` even when hidden. Its view reads GLFW key constants, accepts `ImFont*`, and uses ImGui edit, completion and history callbacks. `UiContext::TextInput` supplies typed results, but the current call does not provide the Console's token/caret editing and Enter/Tab/history behavior. [Console subscription and pump](../../../engine/editor/ui/component/editor_console_component.cpp#L172), [native input flags](../../../engine/editor/ui/component/editor_console_component.cpp#L252), [async completion](../../../engine/editor/ui/component/editor_console_component.cpp#L362), [neutral text input](../../../engine/editor/ui/api/editor_ui/ui_backend.h#L185).

Follow the ED6 `runtime/command/editor` / `CommandEditor` boundary. Keep parsing, history, completion, subscription cancellation and result draining with the Console. Reuse `RuntimeCommand`, `UiContext` and the existing host lifecycle. Introduce a neutral text interaction only for behavior the existing controls cannot preserve. The host binds the toggle intent and provides visibility. Verify hidden completions, caret-aware replacement, Enter/Tab/arrows, shortcut focus and shutdown while a command is pending.

### P2 · Render diagnostics views migrated, SceneEditor adapters remain · Partial

`engine/runtime/render/editor` now contains the neutral GPU Profiler and Debug
Viewer bodies under `RenderEditor`. `GpuProfilerView` consumes Render metrics and a
copied `SceneEditorTimingSnapshot`. `DebugViewerView` owns visibility-driven
demand and asks the host to register its borrowed image. Their two
`EditorWindowComponent` wrappers, timing translation, image-registration wiring
and tool registration remain in `editor_ui.cpp`. Move that wiring into
SceneEditor composition when its host is separated. Preserve `gpu_profiler`
and `debug_viewer` IDs, export fields, optional timestamps and demand release
on tab switch, floating collapse and teardown. The Json header is used by the
profiler implementation. Its CMake edge can be private. [RenderEditor target](../../../engine/runtime/render/editor/CMakeLists.txt), [profiler view](../../../engine/runtime/render/editor/gpu_profiler_view.h), [diagnostic view](../../../engine/runtime/render/editor/debug_viewer_view.h), [remaining wrappers](../../../engine/editor/ui/editor_ui.cpp#L55).

Camera Settings, viewport and gizmo remain SceneEditor tools. The viewport
still requests an `ImTextureID` through the native renderer and implements its
own aspect fit. Use the existing opaque image route after verifying Canvas
input as described below. Render still supplies target extent and scene rays.
SceneEditor maps the fitted image rectangle to pick, capture and gizmo intents.
[Camera view](../../../engine/editor/ui/component/editor_camera_settings_component.cpp#L15), [viewport image and input](../../../engine/editor/ui/component/editor_viewport_component.cpp#L81).

### P2 · Loading tools, menu actions and native host chrome remain mixed · Open

- Loading and Startup Profiler are constructed in `BuildLoadingTree` and cleared after promotion. Keep the loading surface and its model/projection with startup presentation, and place the profiler with the startup/metrics editor adapter. Both currently read the injected snapshot independently. A single frame snapshot at the loading composition boundary can keep the two views consistent. Keep the existing tested snapshot conversion and icosahedron projection local to loading. Preserve partial progress, failure display, CPU-only logo/geometry inputs and the loading/glow path before SceneEditor exists. Reuse Editor icon loading only if it resolves the same intended artwork. [Loading tree](../../../engine/editor/ui/editor_ui.cpp#L1167), [promotion](../../../engine/editor/ui/editor_ui.cpp#L923), [loading draw](../../../engine/editor/ui/component/editor_loading_component.cpp#L303), [startup profiler](../../../engine/editor/ui/component/editor_startup_profiler_component.cpp#L173).
- The live menu renderer is `EditorMainMenuBarComponent`. `BuildMenuBar` creates Settings, captures screenshots through the Runtime service, toggles glow and builds View entries from the dock model. Move action wiring to composition, reuse workspace intents for visibility and keep menu mechanics in the neutral UI/backend. Preserve the main menu work-area inset, current dynamic check state and each registered tool's reopen route. Keep the screenshot service and Settings close/update behavior with their existing owners. [Menu construction](../../../engine/editor/ui/editor_ui.cpp#L347), [menu renderer](../../../engine/editor/ui/component/editor_menubar_component.cpp#L21), [neutral Settings window](../../../engine/editor/settings/editor_settings_window.cpp#L27).
- `EditorUIComponent` includes ImGui despite exposing only render methods. `EditorWindowConfig::extra_flags` carries toolkit flags, and `EditorPanelContent` still has a parameterless draw fallback. `EditorWindowComponent` serves as a panel body, floating host and child tree. The viewport is the sole live `EditorWindowComponent::AddComponent` consumer found. Migrate the live descendants before removing the base/tree. Keep visibility, deferred close and hidden update as first-class lifecycle behavior. `InitializeViewer` and `GetCodeFont` have no in-tree callers in this snapshot. `DrawRenderTarget` has an internal image-resolver caller and can become private when its callers remain internal. [Component base](../../../engine/editor/ui/component/editor_ui_component.h), [window contract](../../../engine/editor/ui/component/editor_window_component.h#L23), [panel content](../../../engine/editor/ui/component/editor_panel_content.h#L17), [image resolver](../../../engine/editor/ui/editor_ui.cpp#L240), [legacy public methods](../../../engine/editor/ui/editor_ui.h#L131).

## Complete file inventory

Stems refer to the `.h` and `.cpp` where both exist. References were checked by file name, declared type and construction/include/target usage across the worktree. Test-only use is distinguished from live composition.

| Files | Current caller or evidence | Disposition |
|---|---|---|
| `editor_button_component`, `editor_listbox_component`, `editor_plot_component` | Compiled in `EditorUILib`; no construction, include consumer or test located | Removed 2026-11-11 with their CMake entries. Current controls and `ui::Plot` already have consumers. |
| `editor_text_component`, `editor_tooltip_component` | Compiled; Tooltip includes Text. No consumer outside this group | Removed together 2026-11-11. Preserve any future tooltip need as a neutral capability with a real caller. |
| `editor_container_component` | Compiled; no consumer; empty container underflows in `Render()` | Removed 2026-11-11; ED1 uses the live tool-row host. |
| `editor_camera_component`, `editor_scene_component`, `editor_drag_component`, `editor_slider_component` | Header-only, no include or symbol consumer located | Removed 2026-11-11 after reference audit. The live Camera Settings and Viewport are separate files. |
| `editor_camera_settings_component`, `editor_viewport_component` | `EditorUI::BuildCameraSettingsPanel` and `BuildViewportPanel` | Migrate to SceneEditor with current Runtime intent sinks. |
| `editor_console_component` | `EditorUI::BuildConsolePanel`; hidden update via tool row | Migrate to `CommandEditor`, preserving input and completion lifetime. |
| `editor_debug_viewer_component`, `editor_gpu_profiler_component` | `EditorUI::BuildDebugViewerPanel` and `BuildGpuProfilerPanel` | Moved 2026-11-11 into `engine/runtime/render/editor/` under `RenderEditor`. SceneEditor injects frame-loop/UI timings and the Debug Viewer's host image registration; visibility still controls diagnostic-view demand. |
| `editor_loading_component`, `editor_loading_view_model`, `editor_loading_icosahedron` | Loading tree, view-model and topology tests | Keep together under startup presentation. Neutralize drawing while retaining the existing value conversion and geometry. |
| `editor_startup_profiler_component` | Loading tree's auxiliary window | Move to startup metrics editor presentation. Preserve its visibility before promotion. |
| `editor_menubar_component` | SceneEditor main menu. The tool row has its own native tab popup. | Keep live menu behavior through the neutral backend. The unimplemented `EditorMenuComponent` type and unused `AddMenu` method are removable independently. |
| `editor_layout_model`, `editor_layout_rect`, `editor_tool_row_model` | SceneEditor, Audio workspace, layout persistence and model tests | Retain headless models in the workspace owner. Move scene IDs and preset construction to composition. `ResetToCompactViewerDefault` has no caller found and can be retired after checking persisted layout compatibility. |
| `editor_layout_participant` | `EditorUI::ApplyLayoutToTree` and `EditorProfileBarComponent` | Keep while the profile bar uses it. Reassess when host-assigned footer geometry replaces that last use. |
| `editor_panel_content`, `editor_window_visibility` | Dock/float lifecycle, neutral Settings window, model and lifecycle tests | Retain lifecycle contracts with the workspace owner. Retire parameterless drawing after live callers migrate. |
| `editor_tool_row_component`, `editor_splitter_handles`, `editor_docked_workspace` | SceneEditor and Audio Player, with dock lifecycle tests | Preserve placement, reorder/float/pin, close and resize. Extract workspace behavior and backend mapping. The Audio facade remains in use until its consumer switches. `row_rect_` is write-only and its assignment can be retired independently. |
| `editor_window_component`, `editor_ui_component` | Scene tools, Log adapter, Asset/Actor panels, extensions, loading and profile bar | Keep as compatibility hosts while consumers are migrated. Reduce to neutral hosting and remove redundant base/child paths after verifying each remaining caller. |

The Audio mixer component already removed in the current worktree is excluded from the inventory. `AudioPlayerEditor::RenderInfo` owns that behavior. `SettingsEditorWindow` and `LogEditorView` already have neutral view paths. SceneEditor still constructs the Settings shell and legacy Log wrapper, so their remaining assembly belongs with ED6.4 and ED6.5. [Dead-code ledger](../../dead_code.md), [Settings composition](../../../engine/editor/ui/editor_ui.cpp#L368), [Log wrapper](../../../engine/editor/log/editor_log_component.cpp#L28).

## Direct collaborators that also need migration

| Current owner and evidence | Proposed disposition | Behavior to preserve |
|---|---|---|
| `editor/profile` status bar and `CreateEditorProfileMetrics`, shared by SceneEditor and Audio Player | Keep the generic metric views and history in `editor/profile`. Move the footer drawing to neutral UI and give the workspace its height through composition. Bind Engine FPS, Render metrics and platform memory inputs in the existing composition-side factory. | The footer serves several domains and is distinct from the Render-owned GPU Profiler. Preserve metric history and optional GPU samples. [Metric factory](../../../engine/editor/profile/editor_profile_metrics.cpp#L14), [SceneEditor caller](../../../engine/editor/ui/editor_ui.cpp#L911), [Audio caller](../../../engine/module/audio/editor/audio_player_editor.cpp#L155) |
| Loading view, icosahedron, view model and Startup Profiler | Place the four groups together in Editor startup presentation, proposed `engine/editor/startup/`. Inject Runtime's `StartupSnapshot` and memory sampling at construction. | Startup Profiler consumes asset-load phases and process memory. Both surfaces must work before SceneEditor promotion. [Startup assembly](../../../engine/editor/ui/editor_ui.cpp#L1167), [Startup Profiler](../../../engine/editor/ui/component/editor_startup_profiler_component.cpp#L173) |
| `editor/gizmo/transform_gizmo` and the viewport | Place the gizmo with the viewport and camera settings in `engine/editor/scene/`. Keep projection, axis picking and drag policy there, and map drawing and pointer events through UI. | Preserve shared Actor selection/edit intent, one fitted image rectangle and capture release. [Gizmo render and interaction](../../../engine/editor/gizmo/transform_gizmo.cpp#L250), [viewport construction](../../../engine/editor/ui/editor_ui.cpp#L695) |
| `editor/actor` outliner, inspector, property view and shared model | Place the tools and model in `engine/runtime/gameplay/editor/` under the planned `GameplayEditor` target. SceneEditor owns the shared model instance and routes viewport selection to it. Keep the Runtime gameplay bridge UI-free. | Preserve outliner/inspector/gizmo sharing, revision checks and transform commits. Inspect numeric drag capture in the inspector during the same migration. [Actor source list](../../../engine/editor/actor/CMakeLists.txt), [Actor composition](../../../engine/editor/ui/editor_ui.cpp#L713), [inspector capture interface](../../../engine/editor/actor/editor_actor_inspector_component.h#L33) |
| `editor/asset` browser/reference models and views | Move the tools to `engine/runtime/asset/editor/` under the planned `AssetEditor` target. Keep catalog access behind the existing snapshot source. | Preserve shared browser/reference selection, explicit refresh and reference export. [Asset source list](../../../engine/editor/asset/CMakeLists.txt), [browser/reference wiring](../../../engine/editor/ui/editor_ui.cpp#L801) |
| `editor_asset_file_dialog` | Move the generic UTF-8 picker contract and OS implementation to Editor platform. The current callers are Audio Player and TTS, so this source stays outside `AssetEditor`. | Preserve file/folder selection, cancellation and multiselect. [Picker contract](../../../engine/editor/asset/editor_asset_file_dialog.h), [Audio caller](../../../engine/module/audio/editor/audio_player_editor.cpp#L328), [TTS caller](../../../engine/module/tts/editor/tts_editor_editor.cpp#L498) |
| `editor/settings/editor_layout_settings` and `editor_panel_command_provider` | Move these with EditorWorkspace. Keep SceneEditor-specific preset keys and layout path selection in composition. | Preserve v1–v3 layout decoding, stable panel IDs, queued command completion and teardown. [Layout persistence contract](../../../engine/editor/settings/editor_layout_settings.h#L24), [workspace command registration](../../../engine/editor/ui/editor_ui.cpp#L537) |
| `editor/log/editor_log_component` and `editor/settings/editor_settings_window` | Complete ED6.4 around the existing `runtime/core/log/editor` and `runtime/settings/editor` views. Keep feature-specific model/view/close policy with its owner and compose generic hosts at the application boundary. | Preserve Log view state and Settings pending edits/save/close behavior. The Settings model/view sources still contribute to `EditorUILib` through CMake. [Log adapter](../../../engine/editor/log/editor_log_component.cpp#L28), [Settings target wiring](../../../engine/editor/settings/CMakeLists.txt), [Settings shell](../../../engine/editor/settings/editor_settings_window.cpp#L27) |
| `EditorExtensionRegistry` and `EditorUIPlatform` | The registry's factory signature still exposes `IEditorImguiRenderer*`, and no in-tree registration caller was found. Recheck supported extension users before retirement. Explicit workspace contributions are the ED6.3 successor. Keep the platform adapters in backend implementation and narrow their CMake visibility after callers switch. | Preserve context and image lifetimes, and the order of platform initialization and shutdown. [Factory signature](../../../engine/editor/ui/editor_extension_registry.h#L18), [platform dependencies](../../../engine/editor/platform/CMakeLists.txt), [current assembly](../../../engine/editor/ui/editor_ui.cpp#L725) |

## Further boundaries found outside `ui/component`

### P1 · The production presentation host was coupled to SceneEditor · Host extracted; smoke/workspace closure open

Audio Player, TTS, Signal Lab, Terrain and Live2D previously called
`EditorUI::InitializeUiViewer` and linked the aggregate `EditorUILib`, which
also compiled SceneEditor assembly. `EditorUiHostLib` now owns context/font,
WSI and renderer lifecycle, `UiSession`, frame begin/submit/end, image
registration and palette loading. `EditorUI` composes that host for SceneEditor;
the five standalone viewers now instantiate the host directly. At the review
baseline, Audio still linked `EditorUILib` through the legacy component host.
It has since moved to `EditorWorkspaceLib`; Audio no longer links `EditorUILib`.
The UI smoke and Settings-fields smoke use `EditorUiHostLib`; their Vulkan and
OpenGL variants compile, but were not launched in this change.
[Host API](../../../engine/editor/ui/host/editor_ui_host.h), [host target](../../../engine/editor/ui/CMakeLists.txt), [SceneEditor composition](../../../engine/editor/ui/editor_ui.cpp#L220), [Audio workspace caller](../../../engine/module/audio/editor/audio_player_editor.cpp#L165), [smoke startup](../../../engine/test/smoke/editor_ui/editor_ui_smoke.cpp#L144).

The Audio migration now supplies its own region tree and stable panel IDs to
`EditorWorkspace`. The generic model owns split values and panel placement;
the host draws region windows, tab groups, floating windows and recovery
controls through `UiContext`. The first implementation exposes split ratios
through neutral sliders; interactive splitter dragging and saved workspace
state are still required. It does not yet arbitrate splitters against multiple
floating bounds. SceneEditor remains on its compatibility host, which retains
the legacy scene-layout reader and settings IDs. Keep SceneEditor promotion,
Actor selection, screenshot and Settings wiring in the composition layer.
Runtime's concrete Editor creation remains tracked under ED6.6.

**2026-11-11 implementation record:** Added the string-ID `EditorWorkspaceModel`
and neutral `EditorWorkspaceLib`; Audio now provides the three-column/two-row
preset and registers its seven panes with feature-owned callbacks/themes.
Removed Audio's dependency on the old `EditorDockedWorkspace` component and its
`EditorUILib` link. Each panel can be closed, floated, docked to any Audio
region, or reopened in the Workspace window. The old Audio workspace wrapper
was removed from the component build. Serial Debug builds of
`EditorWorkspaceLib` and `AudioPlayerEditorUI` passed. No tests or GUI run were
performed; saved layout, drag splitters, visible appearance, multi-float
occlusion and remaining profile footer behavior need follow-up.

### P1 · Canvas image drawing can replace its interaction item · Implementation updated; runtime verification open

The ImGui backend creates a Canvas `InvisibleButton`, invokes the image callback
while visiting primitives and then queries the last item's hovered, pressed,
active, focused and visible state. The production callback used to reach
`DrawSceneImage`, which submits `ImGui::Image` and registers another item.
Terrain consumes Canvas wheel and secondary-drag results.

The implementation now snapshots Canvas interaction and observation immediately
after the Canvas item is created. Image primitives are submitted through the
backend's `ImDrawList` path; GL keeps the flipped UVs and sRGB marker callbacks,
and Vulkan keeps its existing UV orientation. The `EditorUILifecycleTest` target
builds successfully. A headless attempt to seed ImGui hover state was inconclusive,
so this does not establish Terrain interaction behavior. Verify loaded and
missing image cases, wheel and secondary drag in a visible Terrain panel before
moving the scene viewport and gizmo to this route.
[Canvas submission](../../../engine/editor/ui/api/ui_imgui_backend.cpp#L449), [image callback](../../../engine/editor/ui/editor_ui.cpp#L240), [OpenGL image](../../../engine/editor/platform/editor_imgui_opengl_renderer.cpp#L148), [Vulkan image](../../../engine/editor/platform/editor_imgui_vulkan_renderer.cpp#L177), [ImGui image item registration](../../../third_party/imgui/imgui_widgets.cpp#L1066), [Terrain consumer](../../../engine/module/terrain/editor/terrain_editor.cpp#L308).

### P2 · Move backend implementation files and composition data with their consumers · Open

| Current source and evidence | Destination and migration boundary |
|---|---|
| `editor_ui_frame_packet.h`, `editor_ui_glow.h`, `editor_theme.cpp` | Put native draw conversion, marker serialization and font atlas/style application under the private ImGui backend. Loading is a direct glow-marker producer today. Carry its existing emission through a focused presentation operation when migrating loading. Keep theme policy and colors injectable. Preserve blend/color conversion, glow ordering and legacy fallback while its backend tests or callers require it. [Packet types](../../../engine/editor/ui/editor_ui_frame_packet.h#L48), [glow producer](../../../engine/editor/ui/component/editor_loading_component.cpp#L189), [theme initialization](../../../engine/editor/ui/editor_ui.cpp#L235) |
| Bloom shader lookup in `EditorUI::RenderActiveTree` | Move prepared shader selection and delivery to the composition-to-presentation boundary. The frame host consumes the prepared CPU shader inputs and submits through the existing bridge. Keep Render's prepared catalog and Graphics resource retirement ownership. Verify late catalog promotion, absent shaders and image/glow rendering. [Frame shader lookup](../../../engine/editor/ui/editor_ui.cpp#L1351), [prepared catalog API](../../../engine/runtime/render/prepared_render_asset_catalog.h#L119) |
| `editor/settings/editor_settings.{h,cpp}` and `EditorUIInitInfo::background_color_override` | Read the legacy JSON palette in application configuration. Give presentation a neutral color and give the Log view its own level-color table. `background_color_override` currently carries `LogColor`. Live2D and EditorUI both parse the palette today. Preserve current file location, version, keys and fallback values during the split. [Legacy palette types](../../../engine/editor/settings/editor_settings.h#L10), [EditorUI palette read](../../../engine/editor/ui/editor_ui.cpp#L264), [Live2D palette read](../../../engine/module/live2d/live2d_viewer_host.cpp#L315) |
| `SettingsPropertyFields` source target | Put the Settings-specific wrapper beside the Settings model/view under `runtime/settings/editor`. Its API takes `SettingPropertyDefinition` and production usage comes from Settings. Keep an independently linkable fields target for existing unit/smoke consumers during the move. Compare Actor's actual policy with the shared scalar/enum portion after its neutral migration. Keep draft, write authorization and scene transform policy in their owning tools. [Fields API](../../../engine/editor/settings/settings_property_fields_view.h#L13), [production caller](../../../engine/runtime/settings/editor/settings_editor_view.cpp#L283), [Actor policy](../../../engine/editor/actor/actor_property_widget.cpp#L8), [independent smoke](../../../engine/test/smoke/editor_ui/CMakeLists.txt#L19) |
| `editor/context` and `Editor::InitEditorUI` | Use the existing per-instance initialization data in SceneEditor composition. A reference scan found `global_editor_context` consumers only in `editor.cpp`. It stores borrowed services, and `Clear` only clears the Editor pointer. Replace the global with instance-owned bindings and remove the old context wrapper once all includes and callers are gone. Coordinate the Runtime lifecycle hook with ED6.6. [Context storage and clear](../../../engine/editor/context/editor_context.cpp#L7), [sole consumer](../../../engine/editor/editor.cpp#L21) |

## Migration order and acceptance

1. The 10 unused file groups and their source entries have been removed in the current worktree. Verify the affected target closure as part of that cleanup's implementation evidence.
2. Close the capture-release and pane-reopen gaps. Establish the ED6.3 host with explicit SceneEditor and Audio presets. Preserve stable IDs, command registration lifetime, legacy placement, deferred close and occlusion for all floaters. Confirm both consumers use the same lifecycle and can coexist independently.
3. Extract the common production presentation entry point and verify the Canvas image/input seam. Migrate Camera Settings and Console, then the viewport/gizmo. Move the Render diagnostics adapters to SceneEditor assembly and verify the already migrated views. Move loading and startup profiler together, and migrate menu drawing after actions are bound to workspace/application owners.
4. Finish scene assembly for Asset, Actor, Log and Settings and re-audit public/actual transitive dependencies. Remove compatibility entry points only after their in-tree consumers and retained extension contracts have been checked. ED6.6 owns the Runtime cycle and ED6.7 owns final compatibility removal.

For implementation, map each batch to affected targets and preview the selected tests with `ctest -N` before running them. Workspace changes should exercise `EditorLayoutModelUnitTest`, `EditorToolRowModelUnitTest`, `EditorPanelCommandProviderUnitTest` and relevant `EditorUILifecycleTest` cases. Use the recording contracts for new neutral interactions. For presentation changes, verify SceneEditor and Audio Player in visible Debug Vulkan and OpenGL using the existing capture path, including promotion, close/reopen, viewport input, resize, and teardown. A render target or render behavior change also follows the Sponza fixture requirements in the [validation matrix](../../validation_matrix.md). Keep runtime timing or visual acceptance separate from static review.

## Review validation and limits

Level 0. Inspected current source, CMake, tests, stage docs and the dirty baseline. Verified live constructors, source/target membership and positive/negative references. No implementation, build, CTest run, runtime reproduction, new GPU lifetime proof or performance measurement was part of this review. Findings describe current source paths and the checks needed before migration can be accepted.
