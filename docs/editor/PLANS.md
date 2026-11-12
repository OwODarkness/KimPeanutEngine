# Editor architecture and design map

The [Editor README](../../engine/editor/README.md) defines the responsibility
contract: reusable GUI and workspace hosting, editor binding mechanisms,
presentation lifecycle, and UI cost diagnostics. SceneEditor is one application
composition; feature modules own their editor tools. Reusable UI can remain
under `engine/editor/ui` with independent build targets.
Feature and workspace views use its general UI API; ImGui calls/types remain
inside the backend implementation. ED6 must establish this boundary while a
second production GUI backend and Agent-readable GUI snapshots remain follow-up work.

`EditorIcons` owns stable `EditorIcon` names and ImageIO loading.
`EditorIconCatalog` reads the `icons` object in `resouce/icon/setting.json`;
resource paths resolve against that JSON's directory, and explicit configuration
paths support alternate catalogs. PNG filenames are not duplicated in C++.
Missing/malformed configuration and missing entries return diagnostics.
`EditorBuiltinIcons` owns CPU masks; widgets borrow `UiIconMask`
views during drawing, without creating GPU objects or exposing toolkit types.
Asset Browser retains asset-type matching; its custom icon names use the same
JSON catalog and loader. Audio-specific transport/progress/volume
semantics belong to `module/audio/editor` and its `AudioEditorWidgets` target.

The Editor hosts tools and owns UI presentation. Runtime owns simulation,
Render owns scene policy and frame scheduling, and Graphics owns physical GPU
objects and API execution. Components emit presentation data through
`EditorUI`; native integration stays in `EditorUILib` adapters.

Existing behavior, build targets, threading, and known boundary debt are in
[editor_module.md](editor_module.md). The current acceptance ledger is
[TODO.md](TODO.md); working rules are in [AGENTS.md](AGENTS.md).

## Stage designs

| Stage | Design boundary |
|---|---|
| [ED1](.plan/ED1.md) | Tool-row tabs and shared visibility |
| [ED2](.plan/ED2.md) | Layout regions, splitters, and persistence |
| [ED3](.plan/ED3.md) | Magnetic panel placement |
| [ED4](.plan/ED4.md) | Loading-safe CPU-projected wireframe decoration |
| [ED5](.plan/ED5.md) | Selective UI bloom through editor-owned presentation and common RHI commands |
| [ED6](.plan/ED6.md) | Toolkit-neutral Editor UI API/private ImGui backend, reusable presentation, explicit workspaces, feature-owned tools and removal of concrete Runtime-to-Editor dependency |
| [ED6.2](.plan/ED6.2.md) | Detailed neutral API/backend/session design, bounded semantic observations for later AI work, real feature pilot and compile/runtime gates |
| [GUI session snapshots](.plan/ui-session-snapshot.md) | Read-only command proposal for frame-consistent layout, hierarchy, styles and semantics |

## Reconstruction direction

[ED6](.plan/ED6.md) is the accepted architecture for separating reusable UI
components/presentation, workspace hosting, and SceneEditor composition.
Feature editor targets live beside their domains (for example Log and Settings)
while their runtime targets remain UI-free. The current EditorUILib and
EditorUI still combine these layers; ED6.0/ED6.1 inventory and lifecycle work are
accepted, but the toolkit boundary and target split are not implemented.
Preserve the current WSI/Graphics bridge, headless layout models and snapshot/edit
contracts. The [baseline review](.review/ED6.md) records concrete coupling and
reuse problems; the [ED6.0 baseline](.review/ED6.0.md) freezes current target/
toolkit dependencies and UI timing evidence. The
[roadmap](TODO.md#ed6--reusable-ui-and-editor-reconstruction) owns stage
acceptance. ED5 and Settings acceptance are independent.

## Settings window proposal

The proposed [Settings architecture](../settings/PLANS.md) assigns settings-page
presentation to Editor while Runtime owns registration/persistence and each
domain applies its changes. [SET3](../settings/TODO.md) will add a shared settings
window using copied snapshots and queued edits; no settings GUI is implemented
by this plan. Existing palette/log files retain their current owner until an
explicit migration stage.

## UI bloom extension

[ED5](.plan/ED5.md) uses public ImGui draw-list markers and an editor-owned RHI
renderer. Editor owns glow semantics, region policy, draw adaptation, and
logical resource lifetimes. Graphics allocates and retires the physical
objects. Render retains the scene graph and chooses when the editor composite
runs. The implementation now includes copied markers, sampled render-target
metadata, a frame-scoped common RHI callback, linear UI composition, bounded
filter passes, and loading/selection accents. Cross-backend color, selected-row
visual, retirement, and performance acceptance remain in progress; see the
[ED5 execution spec](../../.spec/specs/editor-selective-ui-bloom.md).
This explicitly extends the existing editor-presentation exception; it does
not transfer scene rendering into Editor.

The intended data flow is component emission tags → immutable UI frame packet
→ bounded emission/blur passes → ordered UI composition → presentation.
ImGui core, widget behavior, vertex layout, and vendored sources stay intact.
The loading wire/status currently use layered geometry; no filtered-bloom,
cross-backend visual, or performance acceptance is claimed.
