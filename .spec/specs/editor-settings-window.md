# Editor settings window

- Status: implementation present; acceptance follow-up open
- Owner: Editor presentation; Runtime owns the settings service and stores
- Parent: [runtime settings](runtime-settings.md)
- Stage design: [SET3](../../docs/settings/.plan/SET3.md)

## Operation and acceptance

Expose registered settings in one searchable Editor window. Users can edit a
section draft, apply it through the registry/provider path, save accepted values
to an explicitly selected Project or User scope, and inspect provenance and
terminal outcomes. Editor code receives copied metadata/snapshots through a
narrow Runtime interface; only Runtime's game lane performs file writes.

## Placement evidence

- Existing owners inspected: RuntimeContext settings registry/stores and game
  lane; SettingsRegistry snapshots, edits and provider pump; EditorUI menu and
  floating `EditorWindowComponent`; editor layout/component ownership.
- Placement: RuntimeContext owns `RuntimeSettingsEditorAccess`, which borrows
  the registry/stores and serializes save work on the existing game lane. The
  Editor owns `SettingsEditorModel` drafts and the ImGui window. Those feature
  sources are grouped under `engine/runtime/settings/editor` and compiled only
  into `EditorUILib`; the `RuntimeSettings` target remains headless. UI code
  only sees copied section metadata, resolved values, store diagnostics, and
  request results.
- Rejected candidates: passing `RuntimeContext`, a `SettingsStore`, mutable
  registry, or `RenderSystem` to Editor would expose unrelated ownership and
  permit file or domain operations in the frame path. Render/Runtime settings
  do not depend on ImGui.
- Lifetime: Runtime access is created after settings initialization and
  destroyed after the Editor closes its borrowed component. Provider requests
  continue through the existing Runtime pump; save requests are bounded values.

## Validation

Model tests cover draft/revert/defaults, revision conflicts and rebase,
pending/final provider results, and save results. Runtime bridge tests cover
deferred save pumping, successful persistence, and disk-conflict preservation.
Visible Debug Vulkan/OpenGL checks cover opening the window, Render section
navigation, search/no-results, and restart-required feedback. Compact-window
interaction, keyboard-only editing, live Apply, save/reopen, visible failure
presentation, and requested-versus-active reporting remain acceptance work.
All edits use `SettingsRegistry`; saving is explicit and separate from applying.
