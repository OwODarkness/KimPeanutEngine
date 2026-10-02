# Audio Player panel content and theme checkpoint

- Status: partial
- Date: 2026-10-02
- Parent roadmap: [Audio P1](../../docs/audio/TODO.md)
- Design: [P1 player plan](../../docs/audio/.plan/P1.md)

## What was done

The six-dock player previously matched the draft most closely in Now Playing.
The Library, playlist, Info, and Spectrum bodies used a simpler list,
inspector, and default ImGui histogram. This pass moved their content closer
to the [draft](../../save/audio_gui.png) without adding data the engine does
not provide.

- Library and Playlists gained technical section labels, restrained selected
  states, real queue counts, and a distinct Project Music area. No persistent
  playlists, tags, dates, or draft-only categories were fabricated.
- The queue gained a denser table with a playing marker, duration, sample rate,
  type, and actual Content/session source. Narrow widths use fewer columns.
- Info groups source, format, actions, and output device data. It shows the
  existing runtime fields and an explicit empty state.
- Spectrum now draws the existing 48-bin source FFT in an ImGui draw list, with
  a relative dB grid, frequency labels derived from the selected clip's sample
  rate, and an amber active peak. The input is still the decoded source at the
  played cursor, not measured device output. Its 256-frame FFT and normalized
  levels do not support a calibrated low-frequency or dBFS claim.
- `audio_player_theme.h` centralizes the audio content palette. The preview
  widget and surrounding panel bodies use those tokens. The theme and code
  font are scoped inside `AudioPlayerDockPanel::RenderContent`; the shared
  Editor's window frames, title bars, tabs, and dock host retain their
  original theme.

## Ownership and files

- `engine/runtime/audio/editor/audio_player_theme.h` — Audio Player content
  color/style tokens and scoped ImGui style stack.
- `engine/runtime/audio/editor/audio_player_editor.cpp` — content layout,
  metadata presentation, queue table, and spectrum drawing.
- `engine/runtime/audio/editor/audio_preview_widget.cpp` — existing preview
  colors sourced from the same palette.

This is an Editor presentation change. Asset identity/import, Runtime Audio
playback, TTS, public APIs, and voice lifetime are unchanged by this pass.
The working tree also contains separate M1 Content Library and progress-bar
work; this record does not claim ownership or validation for those changes.

## Validation

- Required level: L3, because the ImGui presentation needs a visible runtime
  check after compilation.
- `git diff --check` — pass (Git reported line-ending conversion warnings).
- `cmake --build build --config Debug --target KimPeanutEngine` — pass after
  the content-only theme correction.
- `check_imgui_cpp.py` — `audio_player_editor.cpp` passed. It reported two
  existing branch-sensitive style-stack warnings in `audio_preview_widget.cpp`;
  that script does not establish a source error in this change.
- A Debug Vulkan Audio Player process started and `audio.status` returned
  `Ready` with an empty error and queue. The earlier launch's `GLFW30` window
  was verified on desktop `Default`; the final rebuilt launch was not separately
  checked for desktop or captured.
- No screenshot comparison, narrow-width/HiDPI pass, track import/playback,
  keyboard interaction, or auditory check was completed after the final build.

## Remaining work

Capture the final Debug Vulkan UI under `save/screenshots/validation/` with a
real track and compare Library, playlist, Info, and Spectrum to the draft at
normal and narrow widths. Check selected/empty states, CJK paths, table
clipping, and the spectrum's labels before accepting this visual pass. P1's
separate reusable TTS transport strip remains open in its
[review](../../docs/audio/.review/P1.md).
