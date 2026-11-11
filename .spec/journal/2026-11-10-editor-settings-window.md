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
