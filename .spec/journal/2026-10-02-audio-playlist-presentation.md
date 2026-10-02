# Audio Player playlist presentation — 2026-10-02

## Scope and acceptance

Refine the bottom-center queue table against the user's cyber-themed draft.
Unselected track names must be cyan and their metadata white. The selected
track must use orange text, a dark orange fill, and one orange outline across
all columns. Preserve filtering, stable track IDs, single-click selection,
double-click playback, clipping, and the compact four-column layout.

## Implementation

- `engine/runtime/audio/editor/audio_player_editor.cpp`: font-relative row
  padding, frozen column headings, explicit name/metadata colors, a restrained
  hover state, and a selected-row outline drawn after table channels merge.
  The outline uses the scrolling table's clip bounds and excludes its header.
- `engine/runtime/audio/editor/audio_player_theme.h`: selected-hover palette
  token shared with the existing selected fill and orange accent.

This is presentation-only: no Audio, Asset, RHI, backend, ownership, or public
API changes. Existing transport/profile/layout working-tree changes are
preserved. Baseline HEAD: `7693fff`, with those unrelated changes present.

## Validation

- `check_imgui_cpp.py engine/runtime/audio/editor/audio_player_editor.cpp`:
  passed the narrow scope-stack check.
- `git diff --check`: passed; Git reports existing line-ending conversions.
- `.\tools\kp.ps1 build KimPeanutEngine` and
  `.\tools\kp.ps1 build AudioUnitTest`: Debug builds passed.
- `.\tools\kp.ps1 test AudioUnitTest`: matched no CTest names. Corrected to
  `.\tools\kp.ps1 test -l audio`: all 27 discovered audio tests passed.
- `cmake --build build --config RelWithDebInfo --target KimPeanutEngine`:
  passed. Linker emitted LNK4098 for MSVCRTD; this presentation change does
  not alter the existing dependency/configuration wiring.
- L3 visible runtime check: the Debug Vulkan player's `GLFW30` window was
  visible on desktop `Default`; its initial selected-row capture showed the
  continuous orange outline. At the user's request, subsequent startup and
  visual checks used the rebuilt RelWithDebInfo executable instead. The Debug
  process exited after normal window close.
- RelWithDebInfo Vulkan PID 14748: `GLFW30` visible on desktop `Default`.
  `audio.import_folder` loaded six real FLAC/MP3 tracks from `asset/music`.
  `audio.status` reported zero pending imports and an empty error. Playback
  elapsed time advanced from 0.021 to 20.992 seconds. Selection of tracks 2,
  5, and 6, playback, seeking, and pause succeeded through the runtime bridge.
- `capture.glfw_window` plus request polling verified cyan names, white
  metadata, and an orange full-row selection at 1920×1080 and 1280×720.
  At 1100×720 the table uses its compact four-column layout. A selected row
  partially below the body is clipped before the footer; the outline does
  not leak into the header or adjacent docks. CJK names remain visible.

Captures under `save/screenshots/validation/`:

- [Wide, playing](../../save/screenshots/validation/audio-playlist-release-vulkan-wide.png)
- [Compact](../../save/screenshots/validation/audio-playlist-release-vulkan-compact.png)
- [Clipped selection](../../save/screenshots/validation/audio-playlist-release-vulkan-clipped.png)
- [Final, paused](../../save/screenshots/validation/audio-playlist-release-vulkan-final-1.png)

The RelWithDebInfo preview remains open for the user, paused on Call of
Silence with six imported tracks. No foreground-focus claim is made.

## Limits

No source or environment blocker remains for this change. OpenGL appearance,
HiDPI, mouse/keyboard selection, and scrolled-header interaction were not
separately exercised. The selection/filter/playback handlers are preserved.
The full integration suite and skill template quality suite were skipped:
this change affects only existing playlist presentation, with no template,
public interface, backend, or build-wiring change. P1's broader dock and TTS
acceptance remains open.
