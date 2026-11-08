# Signal generator workspace — 2026-11-08

## Operation and acceptance

Add a general interactive source workflow to Signal Lab: select sine, cosine,
or square; set frequency, amplitude, phase, and offset; generate a sample block;
select generated signals by name/color; compare their colored traces; and inspect
the selected signal's parameters and measurements. Keep the Runtime Signal
generator headless and keep list/plot/presentation state inside Signal Lab.

## Existing candidates and placement

- Extended Runtime `SignalGenerator` with cosine and 50%-duty square kinds. It
  already owns oscillator phase advancement and block generation, so waveform
  math remains beside sine rather than in the ImGui module.
- Reused `SampleBlock::Create` through `SignalGenerator::Generate` and reused
  `Measure` for each generated block. Kept the bounded eight-entry palette/list,
  selection, color choice, and plot drawing local to `SignalLabEditor` because
  those are presentation state with no Runtime consumer.
- Added a per-entry visibility eye. Hidden signals stay selectable with their
  details and measurements intact, but no longer affect plot scaling, traces,
  or hover readouts.
- Made each color swatch open a scoped ImGui color picker. The selected RGB
  value is stored in the signal entry and immediately recolors its trace.
- Added Overlay and Composite display modes. Composite sums the currently
  visible blocks sample-by-sample, checks for numeric overflow, and reports its
  RMS and peak-to-peak values; it is time-domain addition, not a DFT. Both modes
  share an amplitude range sized to include visible source traces and the sum.
- The generator toolbar defaults to the existing 48 kHz, 480-sample (10 ms)
  example. Each Generate click creates an independent block; overlays share its
  common sample/time axis. Square is currently a naive 50%-duty waveform.

## Validation

- `cmake --build build --config Debug --target KimPeanutEngine` — passed.
- Launched the built Debug executable visibly with Vulkan in `signal-lab` mode.
  Confirmed the default sine is present, then generated cosine and square; the
  list showed three names/colors, all traces overlaid, and the right-side detail
  panel followed the newly selected entry. The existing bottom log stayed
  visible.
- Automated tests were not added or run for this UI task.

## Remaining work

The source toolbar uses a fixed 48 kHz rate and 480 samples per generated block.
Acquisition, continuous updates, variable record length, and waveform editing
after generation remain future work under the Signal roadmap.
