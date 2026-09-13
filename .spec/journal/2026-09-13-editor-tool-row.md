# Editor Tool Row — ED1

- Date: 2026-09-13
- Plan: [ED1](../../docs/editor/.plan/ED1.md)
- Roadmap: [Editor TODO](../../docs/editor/TODO.md)
- Blocker for: AB1.2, which hangs the Asset Browser on this row
- Scope: a tabbed bottom tool band, shared panel visibility, and tab isolate/re-dock

## Result

The Editor's bottom band is now one tabbed tool row instead of independent overlapping
windows. The Log and Console are hosted as tabs; a tab can be isolated into a standalone
window and re-docked; and a **View** menu now exists, sharing one visibility state with
the tab strip.

Three findings shaped the work more than the plan anticipated:

**Closing a panel window was terminal.** `EditorWindowComponent::Render()` gated its whole
body on `if (is_open_)` and passed `&is_open_` to `ImGui::Begin`, so ImGui's close button
latched the window shut forever — nothing could reopen it. Fixed by writing the click into
a *local* `bool open` and routing it to the shared visibility (or `is_open_` when unbound).
This is what makes View > Log able to restore a closed tab.

**`MenuItem::selected` was dead.** The renderer passed a literal `false` for the checkmark
and never read the field. It was deleted and replaced by `std::function<bool()> is_selected`,
queried every frame. Replacing rather than appending is deliberate: the only aggregate
construction site then fails to compile, instead of silently initialising the wrong slot.

**The Console drains deferred results while hidden.** `DrainCompletions()` ran *before* the
`is_open_` early-out. A closed tab is not rendered at all, so the tool row gained a
`PanelPump` hook for panels it does not draw this frame, preserving the old semantics
exactly rather than stranding a completed command result until the console reopened.

Two latent bugs were fixed while rewriting the affected lines: the Console's two
`BeginChild`/`EndChild` pairs were conditional, but ImGui requires `EndChild` for every
`BeginChild` regardless of its return value.

### Semantics settled by this stage

- **One bool per panel.** `EditorToolRowEntry::visibility` is the single value behind the
  tab close X, the detached window's X, the View item, and the Console's `~` key.
- **Entry storage is a `std::deque`.** The row hands each panel a pointer to its entry's
  visibility for the panel's whole lifetime; a `std::vector` would dangle it on growth. A
  regression test pins address stability.
- **Active-tab reconciliation.** When the active tab is closed or isolated, the search runs
  forward from the changed index and then backward, one step at a time, so closing the last
  tab selects its adjacent neighbour rather than the leftmost entry.
- **An isolated tab stays in the strip, dimmed.** Removing it would leave the View
  checkmark with nothing to map to and a closed floating window with no visible way back.
- **The row owns its panels.** They never enter `EditorUI::components_`, so nothing renders
  them twice, and `BeginClosing`, `Close`, and the promotion rollback all stay correct with
  no new cleanup sites — the panels die with their container.
- **The strip is hand-rolled.** `BeginTabItem` owns its active index internally, which
  contradicts a model-owned visibility value, and offers no close button, context menu, or
  drag-out. Core drag-drop is available but unused: since re-dock is an explicit menu
  action, the drag resolves from item state and the global mouse release.
- **Drop resolution is ImGui-free.** `ResolveToolRowDrop` takes a plain rect and point and
  returns `TabStrip`, `Float`, or `None`, so the riskiest logic in the drag path is
  unit-tested instead of only smoke-tested. A degenerate row rect answers `None` rather
  than swallowing the drop as "dock".
- **The row is not collapsible.** A container hosting every tool tab must not fold into a
  title bar and hide the whole strip, so it carries `ImGuiWindowFlags_NoCollapse`.

## Files

- `engine/editor/ui/component/editor_window_visibility.h` (new, header-only)
- `engine/editor/ui/component/editor_tool_row_model.h/.cpp` (new)
- `engine/editor/ui/component/editor_tool_row_component.h/.cpp` (new)
- `engine/editor/ui/component/editor_window_component.h/.cpp`
- `engine/editor/ui/component/editor_menubar_component.h/.cpp`
- `engine/editor/ui/component/editor_console_component.h/.cpp`
- `engine/editor/ui/editor_ui.h/.cpp`
- `engine/editor/ui/component/CMakeLists.txt`
- `engine/test/unit/editor/editor_tool_row_model_test.cpp` (new)
- `engine/test/unit/editor/CMakeLists.txt`
- `docs/editor/TODO.md`, `docs/editor/.plan/ED1.md` (new)
- `docs/editor/editor_module.md`, `docs/status.md`, `docs/dead_code.md`
- `docs/asset/asset-browser/.plan/AB1.2.md` (amended: the browser becomes a tab)

## Validation

- `cmake --build build --config Debug --target EditorUILib` — passed, no warnings.
- `cmake --build build --config Debug` — passed, no warnings from any affected unit.
- `cmake --build build --config Debug --target KimPeanutEngine EditorUILifecycleTest
  EditorToolRowModelUnitTest` — passed.
- `EditorToolRowModelUnitTest` — **20/20 passed**: registration and id lookup, duplicate-id
  rejection, first-open-docked activation, forward-then-backward reconciliation on close and
  isolate, `nullopt` when nothing remains, menu/tab shared state, inert unknown ids, safe
  out-of-range indices, entry count invariance, the active-implies-open-and-docked invariant
  after a mixed sweep, entry address stability across registration, and drop resolution for
  inside, outside, boundary, and degenerate rects.
- `ctest --test-dir build -C Debug -R Editor` — **44/44 passed** (includes
  `EditorUILifecycleTest` and `ActorEditorModelUnitTest`, unmodified).
- `ctest --test-dir build -C Debug` — **623/624 passed**. The single failure is the
  pre-existing `LevelLoaderTest.LoadsCheckedInGameplayLevelFixtures` (checked-in
  `pbr_showcase.level` references `model/rock1-bl/rock2`, absent from the repository
  archive) and is unrelated to this stage.
- Level 3 editor smoke on **both** backends via
  `build/Debug/KimPeanutEngine.exe --graphics-api <vulkan|opengl> --startup-level
  level/sponza.level --agent-port 37373`, captured with `capture.screenshot` and
  `view: engine_window`:
  - `save/screenshots/validation/ED1-vulkan-tool-row-default.png`
  - `save/screenshots/validation/ED1-opengl-tool-row-default.png`

  Both show the application window with the new **View** menu, the **Tools** row in the log's
  former band, the **Log** tab active with its close X, log content hosted inside the row,
  the Console correctly absent (it still starts closed), and every other panel unchanged
  (Viewport, World Outliner, Actor Inspector, Camera Settings, Debug Viewer, Performance
  Profiler, profile bar). Neither run logged an assert, error, or Begin/End mismatch.
- `git diff --check` — clean.

### Not run

The following require input the command transport cannot inject — there is no key or mouse
injection command in the built-in catalogue, and the repo's own convention for editor UI
evidence is human-arranged state plus transport capture:

- Clicking a tab to switch to the Console, and the Console's `~` toggle.
- Isolating a panel by context menu and by drag, and the drag ghost preview.
- Re-docking from the floating window's context menu.
- Toggling Log/Console from the View menu and confirming the checkmark follows a tab close.
- Restoring a moved isolated window's position across a relaunch.

Consequently the `RenderDetachedWindows` path is compiled and reasoned about but was never
executed in a live frame during this stage. That is the largest unverified area.

## Correction (2026-09-13, after the captures above)

**Reported:** the tab strip's layout changed by itself — with one tab the panel body sat
beside the strip, and pressing `~` (opening the Console) moved it below.

**Cause:** a real defect in `RenderTabStrip`. `SameLine()` was emitted after every entry
whenever `index + 1 < count`, and `count` includes **closed** entries. In the default state
(Log open, Console closed) the Log tab is the only one drawn but `1 < 2` still held, so a
**dangling `SameLine()`** was issued. The next item submitted is the panel body's
`BeginChild`, which ImGui then laid out on the same line, beside the tab — and with the
row's remaining width, so the body appeared as a narrow column. Once the Console opened,
the second tab consumed that `SameLine` (tabs correctly side by side) and no trailing one
was emitted, so the body dropped to its own line.

So the row's shape depended on how many tabs happened to be open — the inconsistency the
report described. The `SameLine` decision was also wrong in general: it reasoned about
entry indices, not about the tabs actually drawn.

**Fix:** collect the open entries first, then iterate that list and emit `SameLine()` only
between drawn tabs, never trailing. Verified by capturing both states:

- `save/screenshots/validation/ED1-vulkan-tool-row-one-tab.png` — the real default state
  (Console closed). The body now sits below the strip at full width.
- `save/screenshots/validation/ED1-vulkan-tool-row-two-tabs-temp-override.png` — two tabs,
  same shape. **Provenance: this capture was taken with a temporary initial-open override in
  `BuildToolRow` that was reverted before the final build**, because the transport cannot
  inject the key needed to open the Console. It is layout evidence, not ordinary-input
  evidence.

Both captures show `[Log ✕]` and `[Log ✕][Console ✕]` producing an identical tab-row shape,
so the strip no longer reshapes itself. `EditorToolRowModelUnitTest` 20/20 and the 44 editor
tests still pass; `git diff --check` clean.

This is the second defect in this stage found only by looking at a live frame, after the
conditional `EndChild`. Both were in the ImGui event/layout glue that the model tests
cannot reach — which is the strongest argument for the ED2 layout work giving this surface
real test coverage, or for a scripted panel-state hook.

## Correction (2026-09-13, found during ED2)

**This journal claimed more than it delivered.** It says the terminal-close defect was
fixed by writing the close click into a local `bool` and routing it out. That is true only
for windows with a **bound** `EditorWindowVisibility`, and `SetVisibility` is called from
one place — the tool row's `AddPanel`. For every other window the write-out falls through to
`is_open_ = false`, which latches exactly as before.

So after ED1 the six region panels (Viewport, World Outliner, Actor Inspector, Camera
Settings, Debug Viewer, GPU Profiler) could still be closed permanently, as could the
loading-tree Startup Profiler and the Live2D viewer's log panel. ED1's evidence did not
distinguish bound from unbound windows, so the claim was stated too broadly.

ED2 closed it properly by inverting `HasCloseButton()` to default false, on the rule that a
close button is only honest where something can restore the window. See the correction in
[the ED2 journal](2026-09-13-editor-layout.md) for the full account.

## Remaining risks and unverified areas

- The ImGui event glue (click, drag, popup, menu) has no automated coverage; only the model
  logic beneath it does. A regression there would be caught by smoke, not by tests.
- Keyboard-nav focus is stale for one frame when a focused window stops being submitted.
  Not observed in either smoke run; the fix if it appears is `ImGui::SetWindowFocus(nullptr)`
  on the visibility flip.
- Detached windows drop the base window chrome (lock toggle, focus accent), because the
  base's ratio geometry cannot express an arbitrary dragged position.
- `EditorWindowComponent::width_/height_/pos_x/pos_y` are refreshed only inside the base
  `RenderContent()`, so a panel hosted by the row leaves them stale. Log and Console do not
  read them; this constrains which panels can be hosted until ED2.

## Remaining work

AB1.2 now hangs the Asset Browser on this row instead of adding a fourth floating window,
and its plan was amended accordingly. ED2 introduces the region/splitter layout model and
reflow, which is the prerequisite
for ED3's full magnetic placement — ED1's two-destination drop preview is that mechanism
with a two-entry destination table.

Two loose ends worth a follow-up: `EditorContainerComponent` is compiled but never
constructed and its `Render()` underflows on an empty container; it was left unrepaired per
the dead-code rule and noted in `docs/dead_code.md`. And the tab strip has no reordering or
overflow scrolling, so a future panel count beyond the row width would clip.
