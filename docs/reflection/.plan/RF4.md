# RF4 — World Outliner and Actor Inspector

- Status: implementation landed; runtime/visual validation pending
- Parent roadmap: [Reflection Module TODO](../TODO.md#rf4--world-outliner-and-actor-inspector)
- Cross-stage spec: [Runtime Reflection Module](../../../.spec/specs/runtime-reflection-module.md)
- Depends on: [RF3 — Gameplay Editor Bridge](RF3.md)
- Editor context: [Editor Module](../../editor/editor_module.md)

## Objective

Add two Editor windows backed only by RF2 metadata and RF3 value contracts:

- **World Outliner** lists every Actor present in the latest bounded Gameplay
  snapshot.
- **Actor Inspector** shows the selected Actor, its component instances, and
  their reflected properties.

Clicking an Actor in World Outliner changes one shared Editor selection. Actor
Inspector reflects that selection in the same UI frame. Writable controls
submit value-only RF3 commands and show pending, applied, or rejected state;
they never mutate Gameplay memory directly.

## Concrete design question

Where should selection, snapshot reconciliation, and pending-edit state live so
World Outliner and Actor Inspector remain synchronized across RF3 snapshot
revisions without either panel retaining a Gameplay pointer or independently
consuming bridge results?

RF4 answers with one render-thread-owned `ActorEditorModel` shared by both
panels:

```text
Runtime / RF3
  immutable catalog
  latest snapshot
  edit results
       |
       v
ActorEditorModel                         render thread only
  one snapshot load per Editor frame
  one shared optional ActorHandle selection
  pending requests + diagnostics
       |                             |
       v                             v
World Outliner                    Actor Inspector
select ActorHandle                lookup selected Actor in same snapshot
                                  render descriptors + copied values
                                          |
                                          v
                                  value-only edit command -> RF3 sink
```

## Entry conditions

- RF2 publishes a frozen `IReflectionCatalog` containing flat Gameplay
  component descriptors, property flags, value types, categories, display
  names, range/step hints, and widget semantics.
- RF3 is landed. Runtime exposes `IGameplayEditorSnapshotSource` and
  `IGameplayEditorEditSink`; snapshots and edit results contain only copied
  values and stable IDs.
- RF3 Actor snapshots are sorted by full `ActorHandle`, component instances
  preserve Actor insertion order, and snapshot truncation is explicit.
- `EditorUI` creates workspace tools on the render thread after startup
  promotion and destroys them before Runtime tears down Gameplay, its bridge,
  or Reflection.
- `EditorWindowComponent` supplies movable/lockable ImGui window chrome.
- The current viewport occupies the left 80 percent and Debug Viewer the right
  20 percent of the upper workspace, so RF4 must deliberately update the
  default layout rather than overlap new windows.

## Architecture decision

All RF4 state and widget policy belong to Editor. Runtime continues to own
metadata lifetime, Actor snapshots, request validation, and Gameplay mutation.

Create an `actor/` Editor submodule with a small model and two window
components. `EditorUI` owns the model for the lifetime of the promoted
workspace and builds both windows with references to that same model. A later
viewport-picking feature may select through the model's narrow selection API;
selection must not be private state inside World Outliner.

The RF4 dependency direction is:

```text
Editor actor tools
  -> IReflectionCatalog
  -> IGameplayEditorSnapshotSource
  -> IGameplayEditorEditSink
  -> ImGui

never -> GameplayWorld / Actor / ActorComponent / IReflectionAccess / EnTT
```

The existing `EditorUILib -> RuntimeLib` dependency already makes these clean
interfaces reachable. RF4 does not add a dependency from Runtime, Gameplay, or
Reflection back to Editor and does not widen the known static-library cycle.

## RF4.1 — Shared Actor editor model

Add `ActorEditorModel`, owned by `EditorUI` and used only on the render thread.
It borrows the catalog, snapshot source, and edit sink. It owns:

- the latest `shared_ptr<const GameplayEditorSnapshot>` loaded for the current
  Editor frame;
- `optional<ActorHandle>` selection;
- the next nonzero request ID;
- one pending request per `(ActorHandle, ComponentInstanceId,
  ReflectionPropertyId)` property key;
- the latest terminal diagnostic for each property key; and
- a monotonic local frame counter used only to expire transient UI messages.

An `ActorEditorModelConfig` gives string drafts and retained transient messages
explicit finite limits. Pending request count is already bounded by RF3. Clear
property diagnostics and inactive drafts on selection change; discard terminal
results for an old selection after releasing their pending bookkeeping rather
than accumulating a cross-world notification history.

At the start of every promoted `EditorUI::RenderActiveTree` frame, before any
workspace component renders, call `ActorEditorModel::BeginFrame()` exactly
once. It must:

1. atomically load the latest RF3 snapshot;
2. consume edit results exactly once and correlate them by request ID;
3. clear completed pending entries and retain a short applied/rejected status;
4. reconcile the selected full Actor handle against this same snapshot; and
5. clear selection if the snapshot is absent or the exact handle is absent.

Both panels then read the same immutable snapshot for the complete UI frame.
This avoids an Outliner click and Inspector lookup observing different
revisions. Snapshot revision is for refresh detection only; identity comparison
always includes Actor generation.

If selection disappears because the bounded snapshot is truncated, clear it
and report that it is unavailable in the current snapshot. Do not silently
select another Actor, match by ID without generation, or automatically select
the first list item. A newly opened workspace therefore starts with no
selection and an empty Inspector prompt.

`SelectActor(ActorHandle)` accepts only a handle present in the current model
snapshot. `ClearSelection()` is explicit. RF4 is single-selection only.

## RF4.2 — World Outliner window

Add `EditorWorldOutlinerComponent final : EditorWindowComponent` with title
`World Outliner`. Its `RenderContent()` performs no Runtime call; it reads the
model's frame snapshot.

Rendering rules:

- When no snapshot is available, show `Gameplay snapshot unavailable`.
- When the snapshot contains no Actors, show `No actors`.
- Otherwise, render one selectable row per `ActorEditorSnapshot`, using
  `display_name` as visible text.
- Scope the ImGui item ID with both Actor handle ID and generation. Visible
  labels are presentation, never widget identity.
- Highlight the row whose full handle equals model selection.
- On a left click, call `SelectActor` immediately; Actor Inspector sees the new
  selection later in the same component-tree render pass.
- Use `ImGuiListClipper` because RF3 permits up to 1024 Actors by default.
- Display Actor state as muted secondary text or a compact suffix; do not make
  state part of identity.
- If `snapshot.truncated` or `omitted.actors > 0`, show a persistent warning
  with the omitted count. RF4 does not invent paging in this stage.

The first Outliner is deliberately flat because RF3 exposes Actors, not an
Actor-parent hierarchy. Scene-component attachment is not an Actor hierarchy
contract and must not be inferred into a tree.

## RF4.3 — Actor Inspector window

Add `EditorActorInspectorComponent final : EditorWindowComponent` with title
`Actor Inspector`. Each frame it resolves selection inside the model's retained
snapshot and renders one of these states:

- no selection: `Select an actor in World Outliner`;
- selected Actor absent: clear through model reconciliation and show the empty
  prompt;
- selected Actor present: Actor header followed by component sections.

The Actor header shows copied display name, handle ID/generation, and state.
Each `ComponentEditorSnapshot` gets a collapsible section in insertion order.
Use the frozen type descriptor's canonical name for lookup and a UI-only short
name after its final dot for the visible header. Include component instance ID
and a `Root` badge where applicable so duplicate component types remain
distinguishable.

If a component has an invalid reflected type or no descriptor, show its copied
diagnostic as read-only and continue rendering later components. If the
snapshot itself was truncated, show component/property omission counts at the
top of the Inspector.

Within a reflected component:

1. join every `PropertyValueSnapshot` to its descriptor by component type ID
   and property ID;
2. group rows by descriptor `metadata.category`, preserving catalog/snapshot
   order rather than sorting every frame;
3. use `metadata.display_name`, falling back to stable property name;
4. show `metadata.tooltip` on hover;
5. show copied read errors and unsupported combinations as disabled read-only
   text; and
6. scope every ImGui field ID by Actor ID/generation, component ID, and
   property ID.

The Inspector does not cache descriptor pointers beyond Runtime/Editor
lifetime, does not copy the catalog into panel state, and does not call
`IReflectionAccess`.

## RF4.4 — Widget policy

Select widgets from `ReflectionValueType`, property flags, and neutral
`ReflectionWidgetSemantic`. Keep this dispatch as a pure, unit-testable Editor
function before calling ImGui.

| Descriptor/value | RF4 widget | Commit behavior |
|---|---|---|
| `Bool` | checkbox | Submit once when toggled. |
| Signed/unsigned integer + `Enum` | combo using descriptor enum options | Submit selected numeric option. Unknown current values remain visible as `Unknown (<value>)`. |
| Signed/unsigned integer | `InputScalar` or `DragScalar` | Use optional min/max and positive step; commit when editing ends. |
| Floating point | `DragScalar` | Use optional min/max and step; commit when editing ends. |
| String | bounded `InputText` draft | Commit on Enter or deactivation after edit. |
| Read-only or failed read | disabled text | Never submit. |
| Inconsistent/unsupported metadata | disabled typed value plus diagnostic | Never reinterpret or submit. |

Position, rotation, scale, distance, angle, and color semantics remain scalar
presentation hints in RF4 because RF2 deliberately registered scalar leaves.
Group them by category and use suitable formatting/step, but do not pretend
three unrelated scalar descriptors form an atomic vector/color property.
Compound vector editors and a color picker require a future compound-property
contract if atomic mutation is desired.

For numeric controls, metadata bounds affect the widget but the game-thread
setter remains authoritative. Never assume a UI clamp makes a command valid.
Use sufficient numeric precision to round-trip RF2 values and avoid narrowing
unsigned values through a signed temporary.

## RF4.5 — Draft, pending, and result state

ImGui controls require mutable temporary values, but RF4 must not mutate the
snapshot or claim that a draft is accepted Gameplay state. Store a property
draft only while its widget is active. On commit:

1. build a command from the selected Actor handle, component ID, reflected type
   ID, property ID, and draft value;
2. assign a new nonzero request ID;
3. submit through `IGameplayEditorEditSink`;
4. if queued, discard the draft, mark the property pending, and disable another
   edit for that property until its terminal result arrives;
5. if submission returns `QueueFull`, `Stopped`, or `InvalidArgument`, keep the
   authoritative snapshot value and display the immediate diagnostic.

Pending rows show a small `Pending` indicator. They continue to display the
copied snapshot value; no optimistic value is written into the snapshot.

On terminal result:

- `Applied`: clear pending and show a transient success state; the next RF3
  snapshot remains the authoritative display value.
- rejection: clear pending and show the returned diagnostic inline until the
  next edit attempt or selection change.
- result for a no-longer-selected Actor: clear its pending bookkeeping without
  changing current selection; keep only bounded recent diagnostics if useful
  for logging.

One in-flight request per property avoids queue flooding during drag and makes
correlation deterministic. Drag coalescing, continuous gizmo edits, undo/redo,
and transactions remain RF5 concerns.

## RF4.6 — Workspace composition and layout

Extend `EditorUIInitInfo` with three borrowed pointers:

```cpp
const reflection::IReflectionCatalog *reflection_catalog = nullptr;
gameplay::IGameplayEditorSnapshotSource *actor_snapshot_source = nullptr;
gameplay::IGameplayEditorEditSink *actor_edit_sink = nullptr;
```

`Editor::InitEditorUI` obtains them through the existing `RuntimeContext`
getters. `PromoteToWorkspace` creates actor tools only when all three are
non-null; partial availability is a startup composition error, not a half-live
Inspector. Loading UI does not access the bridge.

Use this non-overlapping first-use layout for the upper 70 percent of the main
viewport:

```text
+----------------------+--------------------------------+--------------------+
| World Outliner       |                                |                    |
| x=0.00, y=0.00       |                                |                    |
| w=0.22, h=0.28       |          Viewport              |    Debug Viewer    |
+----------------------+ x=0.22, y=0.00                 | x=0.80, y=0.00     |
| Actor Inspector      | w=0.58, h=0.70                 | w=0.20, h=0.70     |
| x=0.00, y=0.28       |                                |                    |
| w=0.22, h=0.42       |                                |                    |
+----------------------+--------------------------------+--------------------+
| Output Log: existing x=0.00, y=0.70, w=1.00, h=0.27                       |
+---------------------------------------------------------------------------+
```

The windows retain existing lock/unlock behavior. These ratios are defaults,
not persisted layout policy. RF4 does not add docking or layout serialization.

Construct the model before the two panels and destroy the panel component tree
before the model. Insert World Outliner before Actor Inspector in the component
render order so a click is reflected by Inspector in that same frame.
`EditorUI::Close` clears both and resets borrowed pointers before RF3 shutdown.
No panel may consume results or submit edits after close.

## Proposed source changes

```text
engine/editor/
  editor.cpp                                   # inject RF2/RF3 clean interfaces
  actor/
    CMakeLists.txt
    actor_editor_model.h/.cpp                  # frame snapshot, selection, requests
    actor_property_widget.h/.cpp               # pure dispatch + ImGui rendering
    editor_world_outliner_component.h/.cpp     # Actor list window
    editor_actor_inspector_component.h/.cpp    # component/property window
  ui/
    editor_ui.h/.cpp                           # ownership, BeginFrame, builders/layout

engine/test/unit/editor/
  actor_editor_model_test.cpp                  # selection/result behavior
  actor_property_widget_test.cpp               # descriptor-to-widget policy
  editor_ui_lifecycle_test.cpp                 # dependency/lifetime composition

docs/
  editor/editor_module.md                      # landed panel/data-flow description
  validation_matrix.md                         # RF4 path-to-validation mapping if needed
```

File names may follow local conventions during implementation. Keep
`ActorEditorModel` independent of ImGui so its lifecycle, selection, and command
logic can be tested without a graphics context.

## Implementation order

1. Add `ActorEditorModel` with one-snapshot-per-frame update, exact-handle
   selection reconciliation, request-ID generation, and result correlation.
2. Add pure widget-policy selection for every RF2 `ReflectionValueType`, flag,
   and semantic combination used by Gameplay.
3. Add World Outliner and prove same-frame selection through the shared model.
4. Add Actor Inspector read-only rendering, component identity, categories,
   truncation/read-error diagnostics, and duplicate-type presentation.
5. Add writable drafts, commit rules, bounded pending state, command submission,
   and terminal feedback.
6. Inject RF2/RF3 interfaces through `EditorUIInitInfo`, update workspace
   layout, and verify close-before-bridge teardown.
7. Run focused tests and dual-backend runtime/visual smoke. Record landed facts
   in an RF4 journal before closing the roadmap stage.

## Validation

### Model tests

- `BeginFrame` loads one snapshot and consumes results once per frame;
- selecting an Actor makes it available to Inspector in the same frame;
- selection survives newer revisions containing the exact handle;
- Actor destruction, generation reuse, unavailable snapshot, and deterministic
  truncation absence clear selection without choosing a replacement;
- request IDs are nonzero and do not collide with outstanding requests;
- only one request per property can be pending;
- queued/applied/rejected/immediate-failure paths update bounded UI state
  without changing copied snapshot values;
- results for an old selection release pending capacity without selecting it.

### Panel and widget tests

- Outliner renders empty, populated, selected, duplicate-label, and truncated
  states using full handles as ImGui identity;
- Inspector resolves type/property descriptors, preserves component/property
  order, distinguishes duplicate component types by instance ID, and marks the
  root component;
- bool, signed/unsigned integer, float, enum, and string descriptors select the
  expected widget/commit policy;
- read-only, failed-read, unknown type/property, and inconsistent metadata are
  visible but cannot submit;
- a committed control constructs the exact Actor/component/type/property/value
  command and displays pending then terminal status.

Prefer tests against `ActorEditorModel` and pure widget policy. A narrow ImGui
context harness may cover window lifetime and duplicate widget IDs, but RF4
does not introduce a separate UI-test framework solely to simulate mouse input.

### Lifecycle and runtime proof

- `EditorUI` with all three interfaces promotes both panels; missing all three
  leaves actor tools unavailable with a diagnostic; partial injection fails
  promotion and rolls back cleanly;
- `Close` destroys panels/model before borrowed RF2/RF3 services and remains
  idempotent;
- `EditorUILifecycleTest` remains green with fake presentation backends;
- launch the checked-in startup level on Vulkan and OpenGL, confirm all Actors
  appear, click one row, confirm its Inspector details appear, edit one visible
  transform/light/camera value, and observe pending followed by accepted state
  and the updated snapshot value;
- capture the resulting Editor window where supported. A screenshot proves
  layout/presentation only; the bridge and model tests prove mutation and
  threading behavior.

### Commands

```powershell
.\tools\kp.ps1 build EditorUILifecycleTest
.\tools\kp.ps1 test EditorUILifecycleTest
.\tools\kp.ps1 build GameplayEditorBridgeUnitTest
.\tools\kp.ps1 test GameplayEditorBridgeUnitTest
.\tools\kp.ps1 build KimPeanutEngine
```

Run the full Debug CTest suite because RF4 changes Editor public initialization
and workspace composition. Use the Runtime capture workflow for each graphics
backend after the focused tests pass. If native MSVC remains blocked by the
recorded Windows SDK permission failure, record the blocker separately; do not
replace the missing runtime/visual proof with compilation from another toolchain.

## Acceptance criteria

- [ ] World Outliner lists every Actor included in the latest RF3 snapshot and
  clearly reports empty, unavailable, and truncated states.
- [ ] Clicking an Actor selects its full generational handle and Actor Inspector
  shows that Actor in the same UI frame.
- [ ] Selection survives ordinary snapshot refresh and clears when the exact
  Actor disappears; no fallback Actor is selected implicitly.
- [ ] Actor Inspector shows component instance order, duplicate component types,
  root identity, and all readable editor-visible RF2 properties.
- [ ] Widget selection follows RF2 value types, flags, and neutral metadata;
  unsupported or inconsistent properties remain safely read-only.
- [ ] Writable controls submit only value commands through RF3, permit one
  pending edit per property, and never optimistically mutate snapshot data.
- [ ] Queued, applied, rejected, queue-full, stopped, and stale-target outcomes
  are visible and request-correlated; UI teardown abandons presentation state
  without making post-close bridge calls while RF3 owns shutdown cancellation.
- [ ] Both panels share one render-thread model and one snapshot per frame; they
  expose no Gameplay object pointer, `ReflectionObjectRef`, `IReflectionAccess`,
  or EnTT type.
- [ ] Workspace promotion and close preserve borrowed-interface lifetime and
  keep Loading UI independent of Gameplay inspection.
- [ ] Focused Editor/RF3 tests, full Debug tests, and Vulkan/OpenGL interaction
  smoke pass, with environment blockers and missing visual evidence recorded.
- [ ] Landed behavior and validation are recorded in an RF4 journal before the
  roadmap or status page marks RF4 complete.

## Explicit exclusions

- Actor creation, deletion, rename, duplication, hierarchy/reparenting, or
  component add/remove/reorder commands.
- viewport picking, selection outlines, transform gizmos, keyboard shortcuts,
  multi-selection, and mixed values.
- compound vector/color property mutation when only RF2 scalar leaves exist.
- asset-reference pickers; RF2 has no asset-reference `ReflectionValue`.
- undo/redo transactions, drag coalescing, prefab/level save-back, and dirty
  asset tracking.
- docking, saved layouts, search/filter, favorites, and Actor pagination.
- direct Editor access to live Gameplay objects or reflection access.

## Reference gate

- Godot separates its Scene Tree selection surface from its Inspector and
  routes selected objects through shared Editor selection/history machinery.
  RF4 adopts the shared-selection relationship between panels, but stores a
  generational `ActorHandle` and resolves copied snapshot data instead of
  passing a live object pointer. See Godot's
  [Scene Tree dock](https://github.com/godotengine/godot/blob/master/editor/docks/scene_tree_dock.cpp)
  and [Editor interface](https://github.com/godotengine/godot/blob/master/editor/editor_interface.cpp).
- Dear ImGui's `Selectable` contract leaves selection state with the caller and
  reports clicks, while its ID stack supports stable identity independent of
  visible labels. RF4 uses that immediate-mode pattern with full Actor,
  component, and property identities and keeps durable state in
  `ActorEditorModel`. See Dear ImGui's
  [public widget API](https://github.com/ocornut/imgui/blob/master/imgui.h)
  and [demo source](https://github.com/ocornut/imgui/blob/master/imgui_demo.cpp).
- Piccolo demonstrates a compact engine Editor that builds an object list and
  reflected detail controls in ImGui, but its UI directly reaches live world
  objects. RF4 adopts only the two-surface workflow; RF3 snapshots and commands
  remain the mandatory KimPeanutEngine lifetime/thread boundary. See Piccolo's
  [Editor UI source](https://github.com/BoomingTech/Piccolo/blob/main/engine/source/editor/source/editor_ui.cpp).
