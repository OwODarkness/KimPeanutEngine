# Audio module ownership and Editor icons — 2026-11-11

- Starting state: HEAD `1fbc7f4`, with the existing dirty ED6/Settings work and
  Audio Player restoration preserved. See the task [spec](../specs/audio-module-editor-icons.md).
- Placement gate: `tools/agent/placement.ps1` passed for the old audio editor,
  icon/transport sources, Asset Browser, audio/TTS modules and Runtime audio editor.
- Operation/candidates/placement: the spec records the inspected owners and
  destination boundaries. Editor owns built-in visual resources; the audio
  module owns application policy and specialized editor controls. No GPU owner
  or playback callback lifetime is being changed.

## Implementation and targeted validation

- Moved the controller to `module/audio` and its views/theme to
  `module/audio/editor`. `AudioPlayerController` now uses Audio, Asset,
  AssetImport, Log and Config directly, without the RuntimeLib aggregate.
- Added `AudioEditorWidgets` for neutral transport/progress/volume controls;
  both the player and TTS use it. Removed the old audio transport/icon files
  from general Editor UI. TTS now receives UiContext for these widgets; its
  unrelated legacy view still uses ImGui and is not claimed as migrated.
- Added independent `EditorIcons`: stable IDs, `EditorIconCatalog` JSON parsing
  and image loading, and owning `EditorBuiltinIcons` CPU masks. The user's
  clarification removed the initial C++ filename table. The existing
  `resouce/icon/setting.json` now supplies all builtin paths, including standard
  playback actions. Asset Browser shares catalog parsing/decoding while keeping
  its own asset-type matching and fallback glyphs.
- Engine and independent icon/widget test targets build in Debug. The nine
  new contracts pass, covering all checked-in icons, transparency cropping and
  top-origin masks, relative JSON remapping, missing/corrupt resources, invalid
  configuration, stable instance identities, seek capability and volume clamps.
  The first mask fixture incorrectly assumed PNG writer row order; it was
  corrected against ImageIO's existing write/decode convention.
- Generated project references confirm that Runtime Audio contains no player
  or Editor target; EditorIcons/AudioEditorWidgets/AudioPlayerController do not
  reference RuntimeLib, EditorLib, EditorUILib or ImGui. Widget/icon tests compile
  with poison ImGui headers and link without those aggregate targets.
- The first full build's GoogleTest discovery could not start Scene3DHostTest
  (`0xc0000135`). `dumpbin /dependents` identified its runtime DLL search need;
  adding `build/Debug` and VulkanSDK `Bin` to the validation command's PATH made
  discovery work. The corrected full Debug build passed.

## Final validation and closure

Closed at the user's request on 2026-11-11. No commit was requested or created;
unrelated ED6/Settings working-tree changes were preserved.

Commands and results:

- `cmake --build build --config Debug --target KimPeanutEngine EditorIconsTest AudioEditorWidgetsTest -- /m:1`: PASS.
- `ctest --test-dir build -C Debug -R 'EditorIconsTest|AudioEditorWidgetsTest' --output-on-failure`: PASS, 9/9.
- With `build/Debug` and VulkanSDK `Bin` added to this command's PATH,
  `cmake --build build --config Debug -- /m:1`: PASS, full build.
- `ctest --test-dir build -C Debug --output-on-failure`: 1,272/1,283 passed;
  11 failures. Audio, TTS and Editor tests passed. The failures are eight Asset
  tests, two SettingsStore tests, and one TerrainMesh test.
- Ordinary-environment `ctest --test-dir build -C Debug --rerun-failed --output-on-failure`:
  the two SettingsStore cases passed, confirming the sandbox atomic-file-replace
  access limitation. Eight Asset cases and TerrainMesh still fail. Asset failures
  include unavailable level/model identities, absent catalog nodes and a
  diagnostic-count mismatch (129 versus 1). TerrainMesh expects 24 indices and
  receives 96. Their implementation files were not changed by this task; no
  unrelated repair or full-suite success is claimed.
- Placement checks before/after implementation and `git diff --check` passed.
  Static ImGui scope checking passed for the neutral audio sources. Repository
  search finds no ImGui include, type or call in `engine/module/audio`.
- Controller `.cpp` is byte-equivalent to HEAD after newline normalization;
  only its placement/build ownership changed. Its worker/voice lifetime stays
  with the controller, while icons own CPU masks and widgets borrow views.

Runtime evidence:

- Visible `GLFW30` windows were verified on desktop `Default`. Vulkan Audio
  Player and TTS foreground equality was verified on launch; OpenGL launch was
  visible on Default but the initial foreground request failed, so no initial
  OpenGL foreground claim is made.
- Debug OpenGL and normal Debug Vulkan (validation configuration retained)
  loaded the generated 12-second PCM fixture and same-folder SRT. Device output
  initialized; playback error was empty, subtitles were attached, and elapsed
  time advanced 2.235 → 2.832 seconds (OpenGL) and 2.224 → 2.821 (Vulkan).
- Wide empty/playing and 1280×720 paused captures were exported and inspected:
  `save/screenshots/validation/audio-module-icons-json-{opengl,vulkan}-{empty,playing,compact}.png`.
  Built-in icons, transport, waveform, subtitle and spectrum appear in both backends.
- Read-only TTS Vulkan startup captured an existing durable buffered dialog:
  `save/screenshots/validation/audio-module-icons-json-tts-vulkan.png`. Its shared
  play/mute/volume controls render, `tts.status.error` is empty, and no synthesis,
  settings change or library mutation was requested.
- All three validation windows were closed through their GLFW windows and
  exited normally. Captures, fixture audio, diagnostics and logs remain generated
  artifacts outside the commit scope.

Changed-file groups:

- Controller/host and build wiring: `engine/module/audio/`, removal of
  `engine/runtime/audio/editor` sources and its CMake edge.
- Feature presentation/widgets: `engine/module/audio/editor/`, removal of the
  former `engine/editor/audio` placement and general-UI audio transport/icon files.
- Shared icon catalog/loading: `engine/editor/ui/editor_icons.{h,cpp}`,
  `resouce/icon/setting.json`, Editor CMake, and Asset Browser's icon loading.
- TTS consumer: `engine/module/tts/editor/tts_editor_editor.{h,cpp}` and its CMake.
- Independent contracts: `engine/test/unit/editor/{editor_icons_test,audio_editor_widgets_test}.cpp`
  and test CMake. Updated Audio/Editor/TTS/Signal ownership notes and source links,
  the project status, this journal and the task spec.

Limitations and follow-up:

- Nine broad-suite Asset/Terrain failures remain; the two sandbox-only Settings
  failures are separately identified above.
- The existing compact multi-line subtitle layout can extend past the preview
  border; this ownership/configuration task did not redesign that layout.
- Live network TTS synthesis, perceived audio quality, performance measurements
  and the wider ED6 backend/optional-host acceptance matrix were not tested.
  No performance or full ED6/P1 acceptance claim is made.
