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

Run visible Vulkan validation when the user resumes TE1 acceptance. Investigate
the nine unrelated full-suite failures separately from TTS work.
