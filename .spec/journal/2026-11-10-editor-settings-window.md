# Editor settings window implementation

- Status: implementation present; acceptance follow-up open
- Date: 2026-11-10
- Operation: metadata-driven settings window over the Runtime settings service
- Placement audit before edits: SettingsRegistry/SettingsStore remain Runtime
  owners; Editor receives copied metadata and value snapshots through a narrow
  interface. Editor model owns drafts; Runtime game lane owns save execution.
- Existing owners reused: `SettingsRegistry::SubmitEdit` and provider result
  polling; `SettingsStore::Save`; `EditorUI` main menu, component tree, and
  floating `EditorWindowComponent`.
- New lifetime: Runtime-owned access bridge borrows registry and stores; the UI
  component/model borrow the bridge and are destroyed before Runtime teardown.
- Source organization follows the user's correction: the Settings-specific
  model and page renderer are grouped in `engine/runtime/settings/editor`; only
  the thin `EditorWindowComponent` host stays under `engine/editor/settings`.
  Editor CMake adds those sources to `EditorUILib`; `RuntimeSettings` does not
  compile or link them.
- Validation performed:
  - `cmake --build build --config Debug --target EditorSettingsModelUnitTest EditorUILifecycleTest SettingsUnitTest KimPeanutEngine -- /m:1`
    succeeded for model, lifecycle, settings and engine targets. An initial
    combined rebuild was blocked at link because the validation engine process
    held the executable; after closing that run, the focused engine rebuild
    succeeded.
  - `ctest --test-dir build -C Debug -R "SettingsEditorModelTest|SettingsEditorAccessTest|SettingsRegistryTest|SettingsProviderTest|SettingsStoreTest" --output-on-failure`
    passed 34/34 tests.
  - `EditorUILifecycleTest.exe` passed 4/4 tests.
  - After the source move and view/host split, CMake configuration and builds of
    `EditorUILib`, `EditorSettingsModelUnitTest`, and `SettingsUnitTest` passed;
    the placement audit passed without Runtime-to-Editor includes.
  - Visible Debug Vulkan and OpenGL Sponza runs opened the Settings window.
    Checked Rendering section navigation, path-tracing page, search filtering
    and no-results recovery, and the startup page's restart-required message.
    Latest OpenGL run remains open with Settings visible.
  - `tools/agent/placement.ps1` passed structural placement checks;
    `git diff --check` passed.
  - After the visible run, save-result messaging was scoped to its originating
    section and the Settings view was split from the generic Editor window host.
    The rebuilt `EditorUILib` target passed. The engine stayed open for user
    inspection, so the linked executable was not rebuilt after these final UI
    organization changes.
  - The bundled ImGui checker passes the menu component but reports a false
    positive for `BeginPopupModal` because its token matcher expects a nonexistent
    `EndPopupModal`; the component correctly uses Dear ImGui's `EndPopup`.
- Remaining acceptance: keyboard-only editing, compact layout at narrow window
  sizes, live Apply and final provider feedback, save/reopen in the visible UI,
  visible apply/save failure states, and requested-versus-active reporting when
  those values differ. No settings were saved or edited in the visible engine
  runs.

## 2026-11-11 neutral UI migration follow-up

- Operation and acceptance: move the Settings window, navigation, search,
  property/status presentation, actions, and close confirmation onto the
  current Editor `UiContext` API; the Settings sources must contain no direct
  ImGui calls while draft/apply/save/close behavior remains owned by the
  existing Settings model and view.
- Existing candidates inspected: `SettingsEditorView` currently mixes ImGui
  layout with `SettingsPropertyFieldsView`; `UiContext` already provides
  windows, tables, search inputs, buttons, choices, status text, and stable IDs;
  `EditorWindowComponent` owns an ImGui window and is unsuitable as the Settings
  container once the feature window itself moves to `UiContext::Window`.
- Placement and reason: extend the existing Settings view and Settings window
  adapter in their current Editor-only locations. Reuse the typed property
  renderer and Settings model. Do not add a generic Settings layout abstraction;
  the Settings view owns search/navigation/confirmation presentation, and its
  Editor adapter owns neutral window visibility and close lifecycle.
- Consumers/ownership/validation: `EditorUI` owns the Settings adapter and
  borrowed Settings model/access lifetimes. The adapter opens a neutral window
  within the active Editor UI frame. Validate by building the engine, checking
  source for ImGui leakage, `git diff --check`, and the placement audit; visible
  runtime review remains separate.
- Validation: `cmake --build build --config Debug --target KimPeanutEngine -- /m:1`
  passed after CMake regeneration. `rg` found no ImGui symbols, headers, or
  legacy `EditorWindowComponent` references in the Settings editor sources;
  `git diff --check` passed; the final placement audit passed structural checks.
- Not verified: no visible runtime review was performed for this migration.
