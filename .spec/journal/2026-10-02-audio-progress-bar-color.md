# Audio progress bar color — 2026-10-02

## Scope and diagnosis

Use the user's second screenshot as the appearance contract: a single amber
played section and amber playhead. The working-tree widget instead emitted
cyan across the entire played section, then added an orange rectangle over its
tip. Emission replay accumulated both colors even where the opaque orange
rectangle covered the cyan base geometry, washing the tip toward white.

Only `engine/runtime/audio/editor/audio_preview_widget.cpp` is changed by this
fix. Use the existing `kAmber` token for both fill and playhead, with one
emission scope. Preserve the pre-existing waveform glow, subtitle sizing, and
other unrelated working-tree changes. Baseline HEAD:
`e77c0d57449a4a5ffe5e80414420093fff0e21ab`.

## Architecture impact

This is presentation policy in the existing audio player view. There are no
new resources, dependencies, public contracts, or RHI changes; Dear ImGui and
vendored renderers are untouched. The seek hit area and command path are
unchanged. General bloom occlusion remains outside this focused widget fix.

## Validation so far

- Level 3: a UI appearance fix requires visible runtime captures.
- `.\tools\kp.ps1 build KimPeanutEngine` — Debug build passed.
- `git diff --check -- engine/runtime/audio/editor/audio_preview_widget.cpp`
  — passed.
- The skill's `check_imgui_cpp.py` reported existing style-stack warnings in
  `DrawControlButton` and the playback-rate combo. Manual inspection confirms
  each early-return branch and normal branch pops its matching push once;
  the linear scanner counts both mutually exclusive branches. Those functions
  are unchanged by this fix.
- Both API launches used the visible external execution path; their `GLFW30`
  windows reported `desktop=Default`, `visible=True`.
- Imported `save/audio-preview-validation.wav` (12 seconds), started output,
  muted it, sought to eight seconds, and captured the full GUI through
  `capture.glfw_window`, polling to completion. Both captures show a consistent
  amber fill and playhead at 1920x1080:
  `save/screenshots/validation/audio-amber-progress-vulkan.png` and
  `save/screenshots/validation/audio-amber-progress-opengl.png`.
- Vulkan Debug validation was enabled. After clean close, its
  `KimPeanutEngineLog-2026.10.02-16.47.23.txt` contains no error/VUID entries;
  the existing transient-target identity warning remains. This is a color
  check, not full ED5 resource-retirement acceptance.
- The user requested a real music track after the generated-WAV check;
  `Aimer - 春はゆく.flac` was queued through `audio.import_file`. Real-track
  verification is in progress.

No color-only unit test was added. Full-suite, performance, DPI, and broad
playback-quality checks are outside this reversible widget appearance fix.

## Correction after user clarification

The single amber variant established that its tint is correct, but the user
wants both cyan and orange. Restore the cyan section and short amber tip,
keeping the confirmed `kAmber` tint. Stop the cyan geometry at `amber_min.x`
instead of drawing it underneath the opaque amber segment. Each pixel in the
filled bar now has one emission source; the normal blur transition between
adjacent segments remains. This fixes the widget's hidden-source overlap
without changing shared bloom rendering. Earlier single-color captures are
diagnostic evidence, not the final intended design.

The real FLAC imported successfully in the first OpenGL run; Runtime reported
304.95 seconds duration, zero pending imports, empty error, and an advancing
played cursor. `audio-amber-music-opengl.png` records the single-color variant.
Attempting to select the new track before import completed returned "Track ID
is not in the queue"; it was not a playback or source failure. Final adjacent
segment verification follows below.

### Final two-color Vulkan evidence

- Rebuilt `.\tools\kp.ps1 build KimPeanutEngine` after restoring the cyan
  segment — passed. The previous OpenGL window had already closed when the
  task attempted to close it; the build was not blocked.
- Imported the real FLAC before issuing playback/seek commands. Runtime
  reported `pending_imports=0`, `error=""`, duration 304.95 seconds, and
  `state=Playing`. Started at 25% volume and sought to 180 seconds.
- Full-window capture:
  `save/screenshots/validation/audio-cyan-amber-music-vulkan.png`, 1920x1080.
  The adjacent cyan/amber sections and amber playhead are visually correct.
  Median RGB across an interior cyan patch is `(63,255,255)`; the amber patch
  is `(255,227,77)`, consistent with the prior single-amber capture. Patches
  exclude the normal blurred boundary and playhead.
- After clean close, `KimPeanutEngineLog-2026.10.02-16.53.14.txt` contains
  no error/VUID/fallback entries and reports zero remaining Vulkan binding
  sets in the teardown snapshot. The existing transient-pool warning remains.
- A new visible OpenGL run imported the same FLAC for final verification.

## Follow-up: waveform and whole-window brightness flicker

The user reported Vulkan waveform-background flashing and OpenGL whole-window
dark/light flashing. The earlier `audio-amber-progress-*` captures also show
different OpenGL/Vulkan background levels. A later OpenGL window capture was
entirely black. The window capture uses Win32 PrintWindow/BitBlt, so that black
image alone is not proof of a GPU synchronization failure.

### Root cause and changes

Both transient pools compare storage compatibility (extent, format, attachment
shape), which deliberately excludes load/store operations and clear values.
However, a reused target kept the descriptor from its original allocation.
The background-cleared canvas and transparent emission source share storage
shape. Rotating their roles could clear the emission source to a background
color, then blur and add that background into the UI. In Vulkan, retirement
and available pool entries vary with submissions, explaining the intermittent
waveform rectangle. OpenGL also carried draw state into attachment clears.

- OpenGL acquisition now replaces the reused target's descriptor with this
  lease's descriptor before recording.
- Vulkan acquisition applies the lease descriptor through a checked, native
  manager-only `SetTransientLeaseDesc`; incompatible storage and active targets
  are rejected. Frame-fence retirement remains unchanged.
- OpenGL target begin establishes a full-target scissor and writable RGBA
  mask before clearing, and explicitly disables sRGB conversion for linear
  targets. Draw pipelines continue to set their own color-write policy.

Additional changed files:

- `engine/runtime/graphics/backend/opengl/opengl_backend.cpp`
- `engine/runtime/graphics/backend/opengl/opengl_command_recorder.cpp`
- `engine/runtime/graphics/backend/vulkan/vulkan_render_target_manager.cpp`
- `engine/runtime/graphics/backend/vulkan/vulkan_render_target_manager.h`
- `engine/runtime/graphics/backend/vulkan/vulkan_transient_target_pool.cpp`

Physical ownership, pool storage matching, fences, common RHI contracts, and
Editor/Runtime dependencies are unchanged. The extra method is confined to
the Vulkan backend. The cyan/amber widget fix remains in place.

### Final validation and limits

- Debug `KimPeanutEngine`, `GraphicsContractTest`, and `GraphicsSmoke` builds
  passed. `.\tools\kp.ps1 test -l graphics` passed 31/31. Source diff checks
  passed.
- Both final player instances were visible on the Default desktop and loaded
  the real 304.95-second FLAC before capture. Playback snapshots reported zero
  pending imports, an empty error, Playing state, and advancing time.
- Eight full-window captures on each API were exported and inspected during
  music playback:
  `save/screenshots/validation/audio-flash-fixed-gl-01.png` through `-08.png`,
  and `audio-flash-fixed-vk-01.png` through `-08.png`. All are 1920x1080.
  The player surface at `(1100,440)` is `(2,9,14)` in all 16 images; Library
  at `(20,300)` is `(14,18,26)` in all 16. Animated waveform colors vary as
  expected; there are no fully black frames in this sample. The user also
  reported that the flicker appears fixed. This is sampled visual evidence,
  not a guarantee covering every future frame or window arrangement.
- The final OpenGL instance was closed cleanly before Vulkan verification.
- The external `GraphicsSmoke.exe` run failed in its Vulkan fixture with
  `VUID-vkCmdDrawIndexed-None-08114`: a statically used descriptor at Set 0,
  Binding 5 was invalid. Its short-circuit suite did not reach OpenGL. This
  broader smoke failure is not resolved or classified as pre-existing by this
  task; the audio GUI itself passed the checks above. Do not claim the whole
  graphics suite or ED5 acceptance is complete.
- No full-suite or performance run was done; the focused fix changes backend
  implementation policy without changing common API or module wiring. Wide
  retirement/resize stress and general source-occlusion acceptance remain
  separate ED5 work. Perceived audio quality was not assessed.

## Final requested style: solid cyan, orange-only bloom

The user requested that cyan not emit. Cyan remains an ordinary base-UI
rectangle, ending at the amber tip. Only the amber tip and playhead are inside
an emission scope, and the glow region is bounded to that tip instead of the
entire progress bar. The transient clear-policy and OpenGL clear-state fixes
remain unchanged. Debug `KimPeanutEngine` rebuilt successfully. Earlier
two-emitter captures establish the overlap/flicker corrections; final style
captures are recorded next.

- The solid-cyan Vulkan capture was exported as
  `save/screenshots/validation/audio-orange-only-music-vulkan.png`. Cyan
  measures `(48,211,239)` inside the bar and the background immediately above
  it is `(2,9,14)`, confirming that cyan no longer emits. The user then
  preferred the original single-orange design before the equivalent OpenGL
  style check, so that check was superseded.

## Final preference: one orange progress bar

The user prefers removing cyan from progress entirely. The final widget uses
one `kAmber` fill and playhead inside one emission scope, bounded to the played
section. Waveform and controls retain cyan; the fixed transient clear policy
remains. This supersedes the adjacent-segment and solid-cyan variants.
`.\tools\kp.ps1 build KimPeanutEngine` passed for the final widget.

Final Vulkan run imported the real FLAC, reported Playing with no errors or
pending imports, and exported
`save/screenshots/validation/audio-final-orange-music-vulkan.png`. Inspection
confirms a single orange bar and stable background. The visible player is left
open with the track loaded. The final one-color widget was not relaunched on
OpenGL; the earlier one-color OpenGL capture validated that tint, and the later
eight-frame OpenGL sequence validated the final backend clear fix. The broad
GraphicsSmoke failure remains as recorded above.

## Follow-up: cyan/orange progress and timeline edge fades

The user corrected the progress-bar preference: retain a cyan played segment
and use a separate, true-orange bloom tip at the playhead. The current widget
draws those adjacent segments and scopes emission to the orange tip. The
waveform's existing play pulse remains enabled. Time-axis grid ticks, marks,
and labels now use a smooth opacity ramp over 40 screen pixels at both visible
timeline edges, so labels entering or leaving the window do not pop abruptly.

Debug `KimPeanutEngine` build passed after the widget change. A Vulkan audio
player process answered `audio.status` with `Ready` and an empty queue; there
was no loaded waveform clip for visual confirmation of the edge fade. The user
reported the background flash is solved; the transient-target fixes above were
left untouched. After a local-name cleanup, `AudioPlayerMode` rebuilt
successfully. A subsequent full executable relink was blocked by `LNK1168`
because the visible player process held `KimPeanutEngine.exe` open.

## Progress color correction

The user clarified that the progress bar should be fully orange. The played
portion now uses one orange fill and emits bloom across that full portion; the
cyan/orange split has been removed. Waveform colors and timeline edge fades are
unchanged. The background-flash fix remains untouched.
