# SET3 — Editor settings window

**Status: implementation present; acceptance follow-up open.** Parent: [roadmap](../TODO.md).

## User flow

1. Add a **Settings** entry to the main Editor header menu. Choosing **Open
   Settings** shows or focuses one floating Settings window; it does not create a
   second Editor or a modal dialog.
2. The window has a searchable navigation pane on the left and the selected
   section page on the right. Navigation is generated from registered section
   metadata, grouped by category and labelled from schema values.
3. Selecting **Rendering → Path tracing** displays the current path-tracing
   properties. **Rendering → Startup** displays ray-tracing support and marks
   edits as restart-required. Selecting a property or section does not apply it.
4. The page shows value source and current apply state. The user changes a draft,
   then chooses **Apply**. Live settings stay pending until the Render provider
   confirms application; startup settings update next-launch intent. **Save** is
   a separate explicit action with Project or User destination.

## Ownership and interfaces

- RuntimeContext continues to own SettingsRegistry and the project/user stores.
- Editor owns a `SettingsEditorModel`: selected section, search text, copied
  base snapshot/revisions, draft values, and presentation of request/save
  results. It does not own setting definitions, resolved configuration, stores,
  or domain objects.
- The Editor receives a borrowed, narrow settings access interface from Runtime
  composition. It exposes copied schema/snapshot data, section edit submission,
  edit-result polling, and explicit save requests/results. Save work is serviced
  by RuntimeContext on its existing game lane so a filesystem write does not
  block ImGui rendering. Runtime owns the interface and outlives Editor.
- Do not give Editor `RuntimeContext`, `SettingsStore`, `RenderSystem`, or a
  mutable registry pointer. Do not add preference-specific fields to Engine.
- The model stays ImGui-free and testable. A window component translates
  metadata and copied model state into widgets and user intents. Group these
  Settings-specific Editor sources under `engine/runtime/settings/editor`,
  compiled only into `EditorUILib`; the `RuntimeSettings` target remains
  ImGui-free and Render must not link ImGui.
- Reuse the current SettingsRegistry edit/provider path. Do not create an Editor
  path that directly calls Render settings APIs.

## First-page controls

Use the existing Reflection value and property metadata as the control
description:

- Boolean: checkbox.
- Bounded integer/float: labelled numeric input or slider using metadata range
  and step. Show units/tooltips if supplied by metadata.
- Enum: combo using the schema's stable values and labels.
- Unsupported metadata/value type: retain the page and report the property as
  unavailable instead of silently omitting it.

The first Render page uses the already registered `render.startup` and
`render.path_tracing` sections. Display requested/configured values separately
from active values when they differ. Show provenance (Default, Project, User,
Session), apply policy, capability/unavailable reason, pending request, and the
provider's final diagnostic. A restart-required edit must not imply that the
current device changed.

## Draft and save behavior

- Opening a section copies its current resolved values and revision into the
  model. The window never holds references into registry snapshots.
- Widgets edit only the draft. **Revert** discards it; **Restore Defaults** puts
  schema defaults into the draft. Section navigation preserves drafts.
- **Apply** submits changed properties and the base section revision as one
  transaction. Pending is distinct from applied. On stale revision, keep the
  user's draft, refresh the displayed current values, and offer rebase/discard;
  never silently overwrite another command or UI edit.
- **Save** writes accepted values only, never an unsubmitted draft or pending
  provider request. The user explicitly chooses Project or User. A failed write
  leaves the accepted settings in memory and shows a persistent page error.
- Closing with drafts prompts Apply, Discard, or Cancel. Cancel leaves the
  window open. Save is not implied by Apply, close, or shutdown.
- Search filters category, section, label, and property text. An empty result
  gives an explicit no-results state and a clear-search action.

## Layout and interaction

- Use one resizable floating `EditorWindowComponent`, opened from the header
  menu, with a two-pane child/table layout. Keep navigation width bounded and
  let the form use remaining space; narrow layouts should collapse navigation
  behind a section selector rather than clip controls.
- Reuse the existing Editor menu, window visibility, theme and component
  conventions. Persist window visibility/layout only if the current Editor
  layout mechanism already owns it; do not add a second settings file.
- Use stable ImGui IDs derived from section/property keys. Keep keyboard focus
  and Tab navigation usable; Enter activates buttons, Escape cancels a close
  prompt, and errors are visible without relying on color alone.
- Keep labels, descriptions and source/state text readable at the supported
  DPI. Avoid per-frame copies of the full catalog when the model revision has
  not changed.

## Implementation order

1. Define the narrow Runtime settings access/save request interface and its
   lifetime/thread contract. Route save work through the existing Runtime game
   lane; do not make the ImGui render path perform file I/O.
2. Implement and unit-test the ImGui-free `SettingsEditorModel`: section
   navigation, draft edits, apply/pending/result flow, stale revisions,
   defaults, and save result tracking.
3. Add the generic metadata-driven form component and the floating window.
4. Add the header menu entry, visibility owner, and Render category/section
   navigation generated from the registry.
5. Exercise keyboard navigation, error/unavailable states, and resize/DPI
   behavior before visible backend acceptance.

## Acceptance and validation

- Model tests cover default/session/project/user provenance, draft/revert,
  restore defaults, stale edit conflict, pending/applied/rejected/restart
  feedback, unsupported capability, save success/failure, and close decisions.
- Editor tests cover menu open/focus/close, section selection, search/no results,
  metadata widget selection, stable IDs, balanced ImGui Begin/End and Push/Pop,
  keyboard-only Apply/Revert/Save, and draft persistence across navigation.
- Validate narrow and wide window sizes, long labels/diagnostics, unsupported
  Render capability, no provider, pending work, and failed save.
- Build the Editor and affected Settings/Render tests. Run visible Debug Vulkan
  and OpenGL checks with validation enabled; capture the Settings window and
  verify startup restart intent, live Render apply feedback, save/reopen, and
  clean shutdown. Use the approved external-desktop engine launch path.

## Out of scope

- Audio or Editor preference migration (SET4).
- VSync, window policy, or startup-level ownership (SET5).
- Custom one-off widgets when generic Reflection metadata can describe a field.
- Implicit saving, Engine preference setters, or domain mutation from ImGui.
