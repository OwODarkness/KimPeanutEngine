# Runtime Reflection RF4 Journal — 2026-09-05

## Scope

Implemented the first RF4 World Outliner and Actor Inspector slice on top of
RF2 metadata and RF3 value-only Gameplay editor interfaces.

## Landed changes

- Added render-thread-only `ActorEditorModel` with one snapshot load and one
  edit-result drain per Editor frame.
- Selection stores the complete generational `ActorHandle`; refresh preserves
  an exact handle and clears it when the snapshot is unavailable or omits it.
- Pending requests are keyed by Actor/component/property identity. Request IDs
  are nonzero and unique among outstanding model requests. String drafts and
  transient feedback have explicit finite budgets.
- Added pure `ResolveActorPropertyWidget` policy and value formatting for RF2
  bool, signed/unsigned integer, floating-point, enum, string, read-only, and
  inconsistent-value cases.
- Added World Outliner and Actor Inspector windows. Both consume the shared
  model; the Inspector only reads copied snapshots and frozen catalog metadata.
- Added pending/applied/rejected presentation for RF3 commands and a default
  non-overlapping workspace layout. Runtime injects the three narrow interfaces
  at render-thread UI initialization; partial injection fails promotion and no
  injection leaves actor tools unavailable.
- Added `ActorEditorModelUnitTest` with model lifecycle/result-correlation and
  widget-policy coverage.

## Validation

- `cmake -S . -B build-mingw -G "MinGW Makefiles"` — passed.
- `cmake --build build-mingw --target EditorUILib -j 2` — passed.
- `cmake --build build-mingw --target EditorLib -j 2` — passed.
- `cmake --build build-mingw --target ActorEditorModelUnitTest -j 2` — passed.
- `build-mingw/engine/test/unit/editor/ActorEditorModelUnitTest.exe` — passed,
  3/3 tests.
- Warning-enabled MinGW syntax checks for the four RF4 Editor sources and the
  focused test — passed.
- `EditorUILifecycleTest` compiled but its MinGW link is blocked by existing
  mixed-toolchain third-party archives: MSVC-built GLFW/Assimp/miniaudio
  symbols are incompatible with the MinGW linker. Native MSVC remains subject
  to the recorded Windows SDK permission failure.

## Remaining evidence

Run the native Editor lifecycle target, the full Debug suite, and Vulkan/OpenGL
startup interaction smoke after the local toolchain/SDK permissions are usable.
Those checks are required before RF4 is marked fully complete.

## Correction: late startup service publication

The first live startup trace showed that `InitEditorUI()` runs on the render
thread before the game thread calls `InitializeReflection()`. RF4 initially
captured null catalog/bridge pointers during that presentation-only phase, so
workspace promotion skipped the actor tools. `Editor::PromoteEditorWorkspace`
now refreshes the three borrowed services at the startup commit barrier, after
Reflection/RF3 publication and immediately before `PromoteToWorkspace()` builds
the panels. `EditorUI` still keeps Loading UI independent of those services.
