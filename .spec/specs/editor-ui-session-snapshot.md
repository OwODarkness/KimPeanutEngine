# Editor UI session snapshots

- Status: query projections implemented; runtime measurement follow-up open
- Owner: Codex
- Parent TODO: [Agent-readable GUI snapshots](../../docs/editor/TODO.md)

## Objective

Implement a generic GUI Semantic Interface and session discovery with a one-shot
`ui.snapshot` transport described in the [design](../../docs/editor/.plan/ui-session-snapshot.md).
Audio Player is the first validation consumer, not the owner of the schema.
Results must be immutable values from a successfully submitted frame. The UI
thread publishes the latest capture; read-only queries serialize that capture
on the command caller's background thread without waiting for another frame.

## Current state

`UiFrameResult` already contains optional backend observations, and
`UiSession` retains the most recent frame through its generic semantic
interface. `EditorWorkspaceModel` owns the Audio Player layout and panel state.
`EditorUiHost` publishes a bounded immutable capture after each successful UI
submission. The owning Editor object registers the session query commands and
holds their `CommandRegistration` tokens for its lifetime. `EditorUISessionLib`
serializes immutable captures without depending on ImGui. Application Hosts
register domain commands only; they do not own GUI session registration or
snapshot query state.

## Scope and non-goals

- Publish the latest bounded UI capture only after a successful UI submission,
  then query and serialize it on the command caller's thread.
- Add a session query API for `GetUITree()`, `GetElementInfo(id)` and
  `GetElementStyle(id)` over the last completed frame.
- Keep remote snapshot transport/paging as a consumer of that generic API;
  application-specific summaries must remain feature metadata rather than
  hardcoded top-level schema keys.
- Include available neutral control semantics/styles, frame geometry, Audio
  Player workspace layout, and virtual-list coverage.
- Do not migrate SceneEditor legacy panels, expose ImGui types, execute actions,
  or serialize data on the UI/render lane.

## Invariants

- Runtime command transport never reads `UiContext`, workspace state, or ImGui.
- Snapshot observation is enabled while a snapshot source is attached.
- Captures are immutable and bounded; a query never waits for UI work.
- UI and Workspace own their current state; each Editor owns its session store
  and command-registration lifetime; transport stores immutable copies.
- Structured command payloads must parse as objects and cannot collide with
  scalar result keys.

## Stages

1. Extend observation records and add a value-only Workspace projection.
2. Add a bounded immutable store and publish observations after successful submit.
3. Serialize snapshots on the command caller's thread; the owning Editor
   registers and releases read-only query commands with its UI session.
4. Add structured command JSON output and stable bounded continuation cursors.
5. Build affected targets and inspect a live Audio Player command round trip.

## Acceptance criteria

- [ ] `ui.sessions` reports zero GUI sessions headlessly and the attached Audio
  Player session when its presentation is initialized.
- [ ] `ui.snapshot` without a selector reads the latest submitted frame and
  returns nested JSON directly, with no frame wait or polling.
- [x] Snapshot contains layout, hierarchy, semantic values/actions, declared
  style references, geometry/coverage and virtualized ranges where applicable.
- [x] Generic queries return hierarchy, element details and declared styles
  without knowing Audio Player or SceneEditor domain schema.
- [ ] Overview records use generic `title` and `role` fields, retain layout
  bounds, and attach domain summaries to the relevant elements.
- [ ] Failed submission, cancellation, host close, response limits and paging
  do not expose partial or stale mutable UI state.
- [ ] Snapshot-source frames publish observations; sessions without a snapshot
  source do not collect or retain observation data.
- [x] Backend measured bounds use a documented coordinate system and expose
  active clip intersections, visibility and geometry provenance.
- [x] Tree, element and style queries through a retained `UiSemanticView` refer
  to the same immutable completed frame across subsequent submissions.
- [x] Optional control states and intrinsic values have role-aware meanings;
  unavailable states are distinct from `false`, and roles without values omit
  the `value` field.
- [x] Generic semantic, spatial, and visual projections filter by ElementID,
  SemanticID, role, state, and descendants without exposing geometry in the
  semantic projection.
- [x] Session changes report added, removed, and updated nodes with field-group
  masks from a bounded history; expired or mismatched versions request a full
  snapshot.
- [ ] Audio Player defaults to a single visual panel/control representation
  with content, semantics, geometry, and declared styles; `semantic`,
  `spatial`, `full`, and legacy `overview` remain selectable.
- [ ] Run the changed serializer against a newly captured live Audio Player
  frame and record exact output bytes, named-tokenizer counts, latency, and
  serialization time. The current local artifact comparison is not a run of
  the modified serializer, and no target tokenizer is installed locally.

## Validation plan

Level 3: build Debug Editor/Audio/Command targets and exercise the production
Editor session command transport while the visible UI is running. No full CTest
run is planned for this scoped feature.

## Risks and open questions

The current ImGui adapter does not expose stacking/occlusion or every geometry
primitive. These fields must be omitted or marked unavailable rather than
fabricated. SceneEditor remains partial until it adopts the neutral UI host.

## Audio Player semantics follow-up

The snapshot should identify the same regions, tabs, content and controls
as the running player. Extend the neutral observation records with readable
keys and presentation-independent Canvas roles. Workspace owns the layout
hierarchy and panel placement. Audio's view supplies playback and control
meaning, and the existing snapshot serializer projects a concise layout
overview from the captured Workspace. Verify that the Library tab has a tab
role, its controls descend from the Library panel, and playback controls
expose their units and purpose. Keep existing node IDs and snapshot fields.
