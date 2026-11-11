# Editor

Editor provides a reusable tooling environment: general GUI construction and
hosting, bindings between tools and engine services, and diagnostics for its
own UI presentation work.

This is the responsibility contract for reconstruction. The current
`EditorUI`/`EditorUILib` still combine several of these responsibilities with
concrete scene tools; the boundaries below are not all implemented yet.

## Responsibilities

| Capability | Editor framework responsibility |
|---|---|
| General GUI | Reusable windows, menus, controls, theme, focus and visibility |
| Workspace hosting | Docking, tabs, placement, layout persistence and tool registration |
| Editor binding | Shared mechanisms for metadata-driven controls, snapshot presentation and routing user intents through explicit domain interfaces |
| UI presentation | Backend-neutral lifecycle and input/presentation contracts; the ImGui adapter owns context/fonts, native widget calls and submission through the Graphics presentation bridge |
| UI diagnostics | Timings, counters and traces for UI construction and presentation, with explicit CPU/GPU measurement boundaries |
| Tool lifecycle | Initialization, updates while hidden, workspace activation, close requests and ordered teardown |

The framework supplies these mechanisms. Each feature supplies the domain
adapter, model and view that give them meaning. Feature views build UI through
Editor's neutral context API. ImGui calls and types belong inside the
Editor ImGui backend implementation. Reuse does not require a class for every
widget; focused functions can use the same backend-neutral API.

The current neutral API exposes typed Button, Checkbox, Selectable, Combo,
NumericInput and single- or multiline TextInput operations, plus reusable table,
virtual-list, window and canvas scopes. Virtual lists can be composed inside a
table cell. A view gives each control a stable string ID; the API hashes it to
an internal node ID and checks duplicates/collisions within the frame. Results
preserve the control's value type:

```cpp
auto edit = ui.Checkbox({
    .id = "settings.fog",
    .label = "Fog"
}, draft.fog_enabled);

if (edit.changed)
    draft.fog_enabled = edit.value;

if (ui.Button({
    .id = "settings.apply",
    .label = "Apply"
}).activated)
    draft.Apply();
```

Standard control input/result rules live beside the UIWidgets implementation;
UiContext owns frame/thread/identity checks and backend dispatch. Complex views
should compose neutral canvas, layout and input capabilities when available.
If a real consumer needs an operation those capabilities cannot express, add a
bounded backend capability and tests instead of packing its state into UiValue
or adding a general widget plugin registry.

## GUI backend boundary

```text
Feature editor view -> Editor general UI API -> ImGui backend -> Graphics/platform
```

Public UI contracts use framework-owned IDs, colors, geometry, input events,
image references and result/action values. They expose no `ImVec2`, `ImVec4`,
`ImTextureID`, ImGui flags, draw lists, native GUI pointers or ImGui headers.
Feature modules and SceneEditor use these contracts rather than including or
calling ImGui. General window/control hosting is provided by Editor.

Custom plots, waveforms, gizmos and viewports use focused canvas, image and
interaction contracts, with toolkit-specific drawing implemented inside the
Editor backend. Add these operations for actual consumers. Domain algorithms
and custom-control state stay with their feature owners.

`UIWidgets` now includes a neutral 2D Plot over Canvas for linear/log axes,
ticks, line/bar/point series and pointer values. A feature supplies its data and
style tokens; Audio and Signal Lab share the plotting mechanism without sharing
their palettes or domain-specific waveform/acquisition behavior.

ED6 migrates existing direct calls incrementally; they are legacy dependencies
until migrated. Completion requires feature/workspace code and public UI
headers to be free of direct ImGui dependencies. Toolkit-specific implementation
tests and vendored integrations remain scoped to their backend. A second GUI
backend and an agent-readable UI schema are separate follow-up work.

## Framework, feature tools and Scene Editor

- **Editor framework** supplies general GUI, presentation and optional workspace
  hosting that applications can use independently.
- **Feature editor tools** live beside their domains, such as `log/editor` and
  `settings/editor`, in separate editor targets. A feature's editor session owns
  its UI state; its runtime service retains domain state and behavior.
- **Editor Asset tools** provide native file/folder dialogs for editing actions.
  Callers supply filters and receive UTF-8 paths; platform dialog APIs and wide-
  string conversion stay inside Editor, while Runtime Asset and AssetImport
  remain headless.
- **Scene Editor** is an application composition: viewport interaction, Actor
  selection, gizmos, scene layout and the chosen feature tools. Audio Player,
  TTS, Terrain, Signal Lab and Live2D applications choose the framework
  capabilities and tools they actually need.

General GUI may remain under `engine/editor/ui`. Its reusable targets must be
independent of the concrete Scene Editor and broad RuntimeLib target. Directory
location alone does not establish a dependency boundary. A feature's runtime
target stays independent of its editor target and ImGui, even when its source
directory contains an `editor/` child.

## Binding boundary

```text
Domain snapshots / metadata
            |
    Feature editor adapter
            |
       General GUI
            | user intent
    Feature editor adapter
            |
Validated domain edit requests
```

The framework can render an integer or enum property; the feature decides what
that value means, whether an edit is valid and when it becomes active. Settings
owns configuration application and persistence, while its editor model owns
drafts, navigation and feedback. Logging owns collection and snapshots, while
its editor tool owns filtering, selection and follow-latest behavior.

Bindings use copied snapshots, metadata and explicit request interfaces. Keep
domain validation, game/render lane scheduling, pending/terminal outcomes and
save policy with their existing owners. Tool registration supplies narrow
services explicitly rather than making general UI discover every subsystem.

## Content, hosting and lifetime

Content draws controls into the current region. Window hosts own Begin/End,
chrome, geometry and focus; workspace hosts own docking and placement. A tool
updates snapshots and pending operations independently of its visibility.
Its close policy can accept, defer or cancel a close request; the host maintains
one authoritative open/closed state.

Tool views and callbacks are released before borrowed models/services. ImGui
and presentation lifetime remain on their owning render/UI lane. Render owns
scene rendering and frame scheduling; Graphics owns physical GPU resources,
API execution, synchronization and retirement. Feature views consume the
established presentation contracts.

## UI cost and tracing

Profile different kinds of work separately:

| Measurement | Meaning and source |
|---|---|
| CPU tool update | Snapshot refresh, pending-result processing and tool preparation; define its scope separately from drawing |
| CPU UI construction | Widget/layout construction and draw-data preparation |
| CPU UI submission | Adapter preparation, uploads and command recording/submission work |
| GPU UI execution | GPU timings supplied by Graphics for the relevant UI passes |

The current `EditorUI` exposes `GetLastImGuiBuildTimeMs()`,
`GetLastImGuiSubmitTimeMs()` and `GetLastRenderTimeMs()`. These are CPU elapsed
timings for the current function scopes. In particular, the build scope includes
pre-draw work performed after its timer starts; the submit scope includes work
between the build endpoint and renderer submission endpoint. They do not
establish isolated tool-update cost or GPU execution time. Reconstruction must
document and preserve or explicitly revise these measurement scopes.

Compare performance using rebuilt RelWithDebInfo with matched backend, viewport,
active rendering mode, residency, log workload and visible tools. Debug runs
with Vulkan validation enabled provide correctness evidence.

## Documentation and next step

Work proceeds in two steps. First, ED6 reconstructs the current Editor's
responsibilities, dependencies, feature ownership and lifecycle while keeping
ImGui operational behind the general Editor UI API. Establishing that API
boundary is part of ED6. Afterward, a separate design will address another GUI
backend and a structured UI representation that
agents can inspect and translate back into UI descriptions or interactions.
The meaning and scope of that reverse translation remain to be defined in
that design; no schema or replacement framework is selected here.

The [detailed ED6.2 plan](../../docs/editor/.plan/ED6.2.md) requires stable scoped
control IDs, semantic descriptors, typed results and optional bounded internal
frame observations now. These connect meaning to actual layout without making
JSON serialization, an agent endpoint or a retained UI model part of ED6.2.

Use this contract to build the current and intended dependency matrices. For
each consumer, record the framework capabilities it needs, direct/transitive
target dependencies, included headers, actual calls, state ownership and
thread/lifetime requirements. Classify feature tools separately from generic
GUI and scene editing.

- [Architecture map](../../docs/editor/PLANS.md)
- [Current implementation snapshot](../../docs/editor/editor_module.md)
- [ED6 reconstruction design](../../docs/editor/.plan/ED6.md)
- [ED6.2 neutral API/backend design](../../docs/editor/.plan/ED6.2.md)
- [Baseline review](../../docs/editor/.review/ED6.md)
- [Roadmap and acceptance](../../docs/editor/TODO.md)
