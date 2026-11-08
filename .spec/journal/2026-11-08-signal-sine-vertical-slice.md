# Signal sine vertical slice — 2026-11-08

## Operation and acceptance

Generate a deterministic 1 kHz sine at 48 kHz, write a bounded sample block,
measure its values, and show that exact block in Signal Lab. The headless
Runtime target must remain free of Editor/UI/GPU dependencies. Acceptance for
this slice is analytic sample and statistics coverage, partition continuity,
reset/epoch behavior, a Debug Signal Lab build, and a visible scope trace.

## Existing candidates and placement

- `engine/runtime/core/signal/dft.*` was the prior signal location but only
  held inactive DFT code; it did not own sample storage, generation or signal
  statistics. The new focused Runtime `Signal` target owns these reusable
  operations.
- Audio's waveform presentation is playback-local and does not provide a
  Runtime DSP owner. Signal Lab consumes the Runtime block and view, while its
  ImGui code owns only plot presentation.
- `SampleBlock` uses contiguous channel-major storage. `SampleBlockView` is a
  borrowed read-only span view; caller lifetime requires the source block to
  remain alive and unchanged.

## Changes

- Added a validated `SampleBlock`, epoch/sample index metadata, and borrowed
  `SampleBlockView` in `engine/runtime/signal`.
- Added `SineGenerator` with per-sample phase/index advancement and reset, plus
  scalar mean/RMS/min/max/peak-to-peak statistics.
- Generated 480 samples in Signal Lab initialization and added a bounded
  oscilloscope trace, data metadata/readouts, and sample/time/value hover probe.
- Follow-up GUI refinement removes oscilloscope terminology and dense graticule
  chrome. The workspace now uses separate source, waveform, and readout panels;
  the waveform has sparse amplitude/time guides, a brighter trace, and a hover
  cursor with sample details.
- Reused Editor's existing `EditorLogComponent` in a separately rendered,
  bottom-anchored window. It obtains thread-safe logger snapshots and retains
  the existing virtualized rows, level filter, search, follow-latest, and copy
  behavior. The waveform workspace occupies the area above it, so table sizing
  cannot push the log off-screen; only the log-entry list scrolls when needed.
- Added labeled linear amplitude/time axes to the waveform preview, with six
  time ticks and seven amplitude ticks. The plotted samples now use the displayed
  amplitude range.
- Matched the Signal Lab accent, trace, muted labels, and axis grid to the
  existing Codex navy/slate editor theme rather than introducing a separate
  neon palette.
- Added seven headless GoogleTest cases. Constant, impulse and seeded noise
  sources remain open SG1 work; acquisition remains SG2 work.

## Validation

- `cmake --build build --config Debug --target SignalUnitTest` — passed.
- `ctest --test-dir build -C Debug -L signal --output-on-failure` — 7/7 passed.
- `cmake --build build --config Debug --target KimPeanutEngine` — passed with
  `KPENGINE_ENABLE_SIGNAL=ON`, including Signal Lab compilation.
- The updated ImGui editor passes `check_imgui_cpp.py`; the structural guard
  reports balanced ImGui scopes.
- The axis update compiles in `SignalLabMode` Debug. Linking the final
  `KimPeanutEngine.exe` was blocked by MSVC `LNK1168` because the executable is
  currently open; close the running app and rebuild before launching the update.
- The approved Computer Use launcher rejected the `KimPeanutEngine` executable
  launch (`Computer Use was not approved to use kimpeanutengine`). No GUI window
  or screenshot could be verified. Visible inspection on the `Default` desktop
  remains the acceptance gap.
