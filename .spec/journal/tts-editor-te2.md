# TTS editor TE2 durable dialog library and WAV export

- Status: complete (implementation; visible runtime validation pending)
- Date: 2026-10-03
- Parent TODO: [TE2](../../docs/tts/editor/TODO.md)

## What was done
- Captured bounded immutable provider WAV bytes for stream and buffer synthesis
  in core TTS, preserving the Runtime Audio player as the playback owner.
- Added PCM16 WAV validation/canonicalization and atomic storage for manifests,
  owned artifacts, and collision-safe exports.
- Added durable dialog restore, import, duplicate, delete, export, and buffer
  preview actions through the editor controller, UI, and host commands.
- Preserved malformed manifests for repair and rejected unsafe artifact names,
  symbolic links, and invalid WAV files.

## What changed
- Architecture or behavior: editor-owned durable metadata and files; immutable
  WAV data crosses the core TTS result seam; imported/restored previews use a
  Runtime Audio buffer player.
- Important files/modules: `engine/module/tts/tts_system.cpp`,
  `engine/module/tts/types.h`, `engine/module/tts/editor/`, and
  `engine/test/unit/tts/`.
- Public API or ownership changes: `TTSResult` now exposes a shared immutable
  WAV byte vector. Core TTS still has no Editor dependency; Audio owns voices.

## Validation
- Required level: L2 for WAV/library/controller contracts; visible UI path
  remains an L3 runtime check.
- Command: `cmake --build build --config Debug --target TtsEditorControllerTest TtsEditorLibraryTest TTSUnitTest`
- Result: PASS.
- Command: `ctest --test-dir build -C Debug -R "TtsEditor|TTS" --output-on-failure`
- Result: PASS, 19/19 tests.

## Remaining risks and unverified areas
- Visible TTS window, real provider playback, import interaction, export path
  interaction, and keyboard/layout review were not run.
- The full CTest suite was not rerun for TE2.
- The final manifest symlink audit briefly classified a missing library as an
  error on Windows; the missing-file case was fixed and the final focused run
  passed.

## Remaining work
- Run the visible Debug Vulkan TTS UI acceptance when the user wants runtime
  validation. Synthesis controls remain TE3.

## Documentation and follow-up
- Updated `docs/status.md`, `docs/tts/PLANS.md`, and the TTS editor roadmap and
  TE2 stage plan.

## 2026-10-03 dialog-list presentation follow-up
- The dialog list now filters by ID, text, voice, or job state and clips
  off-screen rows. Compact rows show WAV duration and inline playback or job
  cancellation; duration is read from the canonical WAV header on library load.
- Selected row fill, text, and orange outline follow the Audio Player playlist
  palette. Added a screenshot at
  `save/screenshots/validation/tts-dialog-list-orange-20261003.png`.
- Validation: Debug `TtsEditorMode` and `KimPeanutEngine` builds passed. A
  visible Debug Vulkan TTS run captured the selected row successfully.
- Architecture/ownership: no boundary change; WAV metadata remains derived
  from editor-owned artifacts. The screenshot is a local validation artifact.
