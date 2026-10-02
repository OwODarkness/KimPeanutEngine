# Audio Player playlist icons — 2026-10-02

## Scope and acceptance

Use `resouce/icon/audio/music.png` beside every track name. Replace it with
the existing `play.png` only for the selected track while its authoritative
Audio state is Playing. Paused, stopped, ready, and finished tracks show the
music icon. Keep unselected icons cyan and selected icons orange, with stable
track selection, whole-row borders, clipping, and compact columns preserved.

## Implementation

- `engine/runtime/audio/editor/audio_player_editor.cpp`: load the music mask
  once, reuse the transport play mask, reserve a font-sized icon gutter, and
  draw tinted alpha runs after the selectable's background. The play icon
  replaces the previous ASCII playing marker beside the row number.
- `engine/runtime/audio/editor/audio_player_editor.h`: private CPU music-mask
  storage owned by the Audio Player presentation object.

Assets and vendored ImGui are unchanged. No GPU resource, RHI, dependency,
playback, or public interface changes are introduced. Rendering remains
limited to the visible rows already handled by `ImGuiListClipper`.

## Validation

- Scope-stack checker passed.
- `.\tools\kp.ps1 build KimPeanutEngine`: Debug build passed.
- `cmake --build build --config RelWithDebInfo --target KimPeanutEngine`:
  passed. Only RelWithDebInfo was launched, as requested by the user.
- `.\tools\kp.ps1 test -l audio`: all 27 audio tests passed.
- `git diff --check`: passed; existing line-ending conversion warnings remain.
- L3 runtime evidence: Vulkan PID 6364's `GLFW30` window was verified visible
  on desktop `Default` outside the sandbox. Six real FLAC/MP3 files were
  imported from `asset/music`; status reported no pending imports or error.
  Track 2 played at 20.128 seconds, with one orange play icon and cyan music
  notes on the other five rows. Pausing at 32.954 seconds restored its orange
  music note. The 1100×720 four-column layout retains the icon gutter and
  selected-row outline.
- `capture.glfw_window` plus polling exported
  [playing](../../save/screenshots/validation/audio-playlist-icons-playing.png),
  [paused](../../save/screenshots/validation/audio-playlist-icons-paused.png),
  and [compact](../../save/screenshots/validation/audio-playlist-icons-compact.png)
  captures. The paused wide capture includes a track-path tooltip from hover.

The RelWithDebInfo preview remains open and paused for the user. No
performance, foreground focus, or final shutdown claim is made. OpenGL,
HiDPI, and keyboard interaction were not separately checked. Full integration
and skill template regression suites were skipped because the change stays
within existing presentation and a private CPU cache, without template or
shared public interface changes. No source blocker remains.
