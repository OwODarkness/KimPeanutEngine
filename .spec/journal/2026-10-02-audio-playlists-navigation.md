# Audio Player Playlists navigation — 2026-10-02

## Scope and acceptance

Improve the bottom-left Playlists dock against the user's draft. Give the
existing session collection an orange outline, folder icon, centered label,
and right-aligned count; replace the prominent explanation with compact
duration and session information. Keep the count and duration derived from
the real queue and render a useful empty state. Selecting Current queue
should show the track-list tab and restore its All view with no search filter.

## Implementation

- `engine/runtime/audio/editor/audio_player_editor.cpp`: a stable-ID ImGui
  selectable with folder/border decorations, clipped label, queue duration,
  empty hint, and activation handled through the existing dock model.
- `engine/runtime/audio/editor/audio_player_theme.h`: font-relative collection
  row/icon/padding measurements, using the existing selected/hover palette.

No persistent playlists or categories are introduced. This is presentation
and navigation within the Audio Player; playback/import/Asset/RHI ownership
and public contracts remain unchanged. Prior track-table and unrelated
transport/profile/layout working-tree changes are preserved.

## Validation and acceptance

- Scope-stack checker and `git diff --check` passed.
- `.\tools\kp.ps1 build KimPeanutEngine`: Debug build passed.
- `cmake --build build --config RelWithDebInfo --target KimPeanutEngine`:
  passed, with the existing LNK4098 MSVCRTD dependency warning.
- `.\tools\kp.ps1 test -l audio`: all 27 audio tests passed.
- Only RelWithDebInfo was launched for this task, as requested by the user.
  Vulkan `GLFW30` windows for PIDs 29376 and 11928 were verified visible on
  desktop `Default` outside the sandbox. The first preview closed normally;
  its teardown log reported no remaining backend resources.
- Six real project FLAC/MP3 files populated the second preview, with zero
  pending imports and an empty error. The collection count was `[006]` and
  summed duration `27:21`. The empty panel and the populated compact panel
  rendered the orange folder row, aligned count, border, and session caption.
- Computer Use clicked Output Log, then Current queue: the track-list tab
  became active. Clicking Recent, then Current queue restored the All view
  and normal track order. The 1100×720 compact layout retained the aligned
  count and readable label. Search clearing is implemented but was not
  separately exercised by text entry.
- Runtime registry captures and polling produced
  [empty](../../save/screenshots/validation/audio-playlists-navigation-empty.png)
  and [compact](../../save/screenshots/validation/audio-playlists-navigation-compact.png)
  evidence. The hovered row's tooltip is present in those captures.
- The user pressed physical Escape to stop Computer Use before the final
  tooltip-free wide capture/close check. No further Computer Use actions were
  taken. The user then explicitly confirmed the result: "I confirm it is ok".
  The pending final `capture.glfw_window` request did not complete within the
  polling limit; no final wide capture is claimed.

No source blocker remains. OpenGL, HiDPI, keyboard activation, and a final
second-preview shutdown check remain unverified. Full integration and skill
template regression suites were skipped because this change is confined to
existing presentation/navigation and does not alter templates or public APIs.
