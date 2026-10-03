# Standalone TTS editor TE1

- Status: partial; visible runtime acceptance pending
- Date: 2026-10-03
- Spec: [TE1 spec](../specs/tts-editor-te1.md)
- Parent TODO: [TE1 ledger](../../docs/tts/editor/TODO.md)

## What was done

- Registered `--mode tts` with a standalone application host and an
  Editor-facing TTS target.
- Added versioned settings at `project_root/tts/settings.json`. The first run
  creates a blank template; malformed JSON remains intact. Address, port,
  server-side reference audio, and reference text have no compiled machine
  values. Settings support up to 16 voice presets and a stream/buffer choice.
- Added an asynchronous controller, copied UI snapshot, session dialog rows,
  failure and cancellation states, and host commands. Each job captures its
  voice and playback mode. Provider callbacks pass results through an inbox;
  cancellation, connection reset, eviction, and shutdown retire Audio handles.
- Reused `EditorUILib::DrawTransportStrip`. Stream preview has no seek or rate;
  buffered preview has both. Until the TTS result hands off a player, the UI
  shows the job state and cancel action without inventing a played cursor.
- Kept WAV export and provider-dependent synthesis controls out of TE1 because
  their required contracts are not present.

## Architecture and ownership

Core `TTS` has no Editor dependency. The TTS editor host owns the controller
and presentation. Runtime Audio owns player objects; the controller retains
generation-checked handles for session playback. Worker callbacks do not call
ImGui. Runtime mode parsing and module bootstrap registration changed, but
no Render or RHI resource contract changed.

## Validation

- Required level: L4, because CMake target dependencies and Runtime mode
  registration changed. Visible GUI evidence is additionally required.
- `cmake -S . -B build -G "Visual Studio 17 2022"` — PASS.
- `cmake --build build --config Debug -- /m:1` — PASS.
- `git diff --check` — PASS; Git printed line-ending conversion warnings.
- `check_imgui_cpp.py engine/module/tts/editor/tts_editor_editor.cpp` — PASS.
- `ctest --test-dir build -C Debug --output-on-failure` — 1115/1124 passed;
  all 19 TTS tests passed. Nine Asset/Terrain tests failed on archive fixtures,
  catalog expectations, import diagnostics, or a terrain index-count assertion.
  The first failure was a level fixture's missing archived logical model
  `model/rock1-bl/rock2`. No TTS test failed.
- Visible Debug Vulkan validation and captures — NOT RUN. The user explicitly
  deferred the TE1 window pass while moving to TE2. A launched TTS process
  was closed through its `GLFW30` window without claiming visual acceptance.

## Remaining risks and unverified areas

- No local `tts/settings.json` or real GPT-SoVITS server was available before
  the runtime pass. The blank first-launch path and real speech require
  separate runtime observations.
- Wide/compact layout, keyboard focus, foreground GLFW window, and clean GUI
  shutdown need visible checks outside the sandbox.

## Remaining work

Run keyboard-focus review in the visible editor. Investigate the unexpected
duplicate status and the nine unrelated full-suite failures separately.

## Runtime validation (2026-10-03)

- Built the focused Debug targets and ran
  `ctest --test-dir build -C Debug -R "Audio|RuntimeLaunchOptionsTest|TTS|TtsEditor" --output-on-failure` — 81/81 passed.
- Launched `build/Debug/KimPeanutEngine.exe --mode tts --graphics-api vulkan --agent-port 37373`
  outside the sandbox. Verified the visible `GLFW30` window was foreground on
  the Default desktop at launch. The Debug build enables the Khronos validation
  layer when available; that layer was listed locally and the run log had no
  validation errors.
- With blank settings, `tts.generate` failed locally with
  `Set the TTS server address`; no provider request was made. After the user
  supplied a local GPT-SoVITS endpoint and Japanese reference voice pair, the
  ignored `tts/settings.json` was updated without adding those private values
  to the repository.
- The configured runtime reported no generation blocker. A short Japanese
  request completed with job state `Completed`, audio state `Finished`, and
  no error. The endpoint returned 405 to a non-synthesis `OPTIONS` probe, then
  accepted synthesis through its expected POST path.
- Captures: `save/screenshots/validation/tts-te1-debug-wide-20261003.png`
  (1920x1080) and
  `save/screenshots/validation/tts-te1-debug-compact-live-20261003.png`
  (800x650). Both were reviewed. The process exited after a close request to
  its GLFW window.
- Keyboard focus inside an ImGui text field remains unverified because native
  desktop input automation was unavailable. The final runtime status also
  reported `Dialog duplicated` and two entries; no duplicate command was sent
  by this validation pass, and the session library was left untouched for user
  review.

## Finished stream replay correction (2026-10-03)

- The user reported that completed streaming speech could not play again until
  the editor restarted. The UI disabled Play for a finished stream, and the
  controller rejected that action because the consumed stream ring cannot be
  rewound.
- Finished, saved streams now enable Play. The controller retires the consumed
  stream player, loads its already-persisted canonical WAV into a buffer
  preview, and starts that preview. This makes replay local and does not submit
  another synthesis request.
- `cmake --build build --config Debug --target TtsEditorMode` — PASS.
- `cmake --build build --config Debug --target KimPeanutEngine` — PASS.
- The rebuilt TTS editor launched and loaded its saved settings. No test suite
  or playback replay was run in this correction pass.

## Audio editor preview controls (2026-10-03)

- TTS preview transport now uses the Audio editor's play, pause, and stop icon
  masks through `EditorUILib`; the TTS target does not depend on
  `AudioPlayerMode`.
- Speech volume is presented with the Audio editor voice icon, a slider, and a
  percentage readout. Buffered previews use the same discrete playback-rate
  choices as Audio editor; streams still do not expose seek or rate controls.
- `cmake --build build --config Debug --target TtsEditorMode` — PASS.
- `cmake --build build --config Debug --target KimPeanutEngine` rebuilt its
  libraries but could not relink the executable: LNK1168 reported the existing
  TTS editor process (PID 8932) has `KimPeanutEngine.exe` open. The live process
  was preserved; the updated UI will appear after that process closes and the
  executable is rebuilt/restarted.
- No tests were run.

## Compact audio preview controls (2026-10-03)

- Moved TTS Play/Stop, buffered speech speed, and voice volume into one row.
  The voice control now uses Audio editor's filled progress interaction and
  percentage display; the shared `EditorUILib` widget also replaced the
  duplicate implementation in Audio editor.
- `cmake --build build --config Debug --target TtsEditorMode` — PASS.
- `cmake --build build --config Debug --target AudioPlayerMode` — PASS.
- `cmake --build build --config Debug --target KimPeanutEngine` — PASS. The
  TTS editor restarted, settings loaded, and its GLFW window was verified on
  the Default desktop in the foreground.
- `capture.glfw_window` exported
  `save/screenshots/validation/tts-controls-row-20261003.png`; visual review
  confirmed the transport, speed selector, and filled voice control share one
  row. No tests were run.

## Preview control spacing and mute button (2026-10-03)

- Added horizontal/vertical padding, widened Play/Stop to Audio editor's
  78×36 buttons, reversed their order, and widened the speed selector to match
  Audio editor. The voice icon is now a 60×36 mute/unmute button; unmuting
  restores the last audible volume. TTS and Audio editor share the filled
  volume progress control.
- `cmake --build build --config Debug --target TtsEditorMode` — PASS.
- `cmake --build build --config Debug --target AudioPlayerMode` — PASS.
- `cmake --build build --config Debug --target KimPeanutEngine` — PASS. The
  rebuilt TTS editor is visible on the Default desktop.
- `capture.glfw_window` exported
  `save/screenshots/validation/tts-controls-row-padding-20261003.png`; visual
  review confirms the padded row and mute button. No tests were run.

## Portrait and artifact waveform preview (2026-10-03)

- Added canonical WAV preview extraction for sample rate, channel count,
  duration, and 384 peak buckets. Durable dialogs calculate this lazily when
  selected; completed and imported WAVs cache the result.
- Added the user-supplied Kurisu portrait as UI artwork, decoded through ImageIO
  once and cached as a compact CPU-side raster. No GPU texture owner or
  AudioPlayerMode dependency was introduced.
- The preview combines the portrait, text, real waveform, playback cursor,
  elapsed/duration label, and click-to-seek for buffered clips. Streams without
  returned WAV bytes show a waiting label instead of invented waveform data.
- `git diff --check`, Debug TtsEditorMode build, and Debug KimPeanutEngine build
  passed. Visible Debug Vulkan TTS runtime loaded the saved dialog and showed
  the supplied portrait and audio-derived waveform with elapsed/duration and
  playhead on the window. Capture:
  `save/screenshots/validation/tts-avatar-waveform-final-20261003-1.png`. No
  tests were run.
