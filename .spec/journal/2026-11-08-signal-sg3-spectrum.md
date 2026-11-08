# Signal SG3 windows and spectrum measurements

## Operation and acceptance

Build periodic Hann and rectangular windows and compute a real-input one-sided
spectrum with coherent-gain-corrected peak amplitude and power spectral
density. Specify zero padding explicitly. Validate window coefficients, tone
amplitude, DC/Nyquist endpoint scaling, and PSD integration against independent
time-domain weighted mean-square calculations.

## Ownership and reuse

Runtime `Signal` owns the operation beside its DFT/FFT algorithms. `ComputeSpectrum`
uses the existing radix-2 FFT; `BuildWindow` returns coefficients for inspection
and later reuse. Signal Lab remains a consumer. No Audio dependency, UI code,
GPU resource, or new target is introduced.

## Implementation and validation

- Added periodic Hann (`0.5 - 0.5*cos(2*pi*n/N)`, N>=2) and rectangular windows.
- Added explicit-size `ComputeSpectrum`; zero padding is enabled only when the
  caller supplies a larger power-of-two FFT size. Results contain one-sided
  frequency, coherent peak amplitude and PSD bins, coherent gain, and window
  energy. Detrending remains off.
- Corrected only interior one-sided bins by two; DC and even-length Nyquist
  endpoints use factor one. PSD normalization uses the observed window energy.
- Tests check the coefficients and gains, both window types on coherent tones,
  DC/Nyquist endpoints, zero-padding frequency spacing, and integrated PSD
  against independently summed windowed sample power. A 48 kHz / 1 kHz small
  DFT oracle and a coherent power-of-two FFT fixture both produce 1 V peak and
  0.5 V² integrated rectangular-window PSD.
- `cmake --build build --config Debug --target SignalUnitTest` passed after
  CMake regeneration. `ctest --test-dir build -C Debug -L signal
  --output-on-failure` passed 47/47.

## Experiments and Signal Lab view

- Added a 7 kHz tone sampled at 8 kHz fixture whose measured spectrum peaks at
  the predicted 1 kHz alias. An off-bin tone fixture verifies periodic Hann
  reduces far sidelobes compared with rectangular. Both are numerical tests;
  no display interpolation is used as correctness evidence.
- Added a Spectrum view for the selected generated signal. It plots one-sided
  frequency bins on an Hz axis and switches between coherent peak amplitude
  (V) and PSD (V²/Hz), with rectangular/periodic-Hann selection and explicit
  512/1024/2048/4096 FFT sizes. The pointer readout reports bin, frequency,
  amplitude and PSD; the inspector reports bin spacing, coherent gain, peak AC
  frequency/amplitude and integrated PSD.
- The draw buffer now includes all 2049 bins for a 4096-point real FFT, including
  Nyquist. Peak readout ignores DC when AC bins exist. Controls wrap after the
  window selector when the center panel is narrower than the control row; that
  compact layout has not yet had a separate visual resize check.
- `cmake --build build --config Debug --target SignalLabMode KimPeanutEngine
  SignalUnitTest` completed after the running GUI was closed to release the
  executable link lock. The ImGui checker reports unbalanced Begin/End and style
  stacks in its lexical scan; source review and the successful C++ build show
  the editor's conditional `Begin`/`End` pairs are balanced, so this script
  result is a false positive for this file's control flow.
- Visible OpenGL Signal Lab inspection confirmed Spectrum selection, Hann and
  4096-point settings, amplitude and PSD axis labels, and the spectrum plot.
  The view uses the existing ImGui theme and Runtime `Signal` measurements;
  Signal Lab owns only display selection and rendering. Foreground GLFW-window
  and desktop-ID checks were not performed for this Spectrum UI pass.

Saw/triangle generators, two-source mixing, and phase view remain open for SG3.

## SG3 waveform, mixing and phase completion (2026-11-08)

- Added naive periodic sawtooth `A*(phase/pi - 1)` and triangle
  `A*(1 - 2*abs(phase/pi - 1))` sources in Runtime `SignalGenerator`, retaining
  the configured amplitude, offset, phase and sample clock. They are labeled
  naive/unbandlimited in Signal Lab; the harmonic-distortion control is hidden
  for these shapes because their harmonic series is intrinsic. Noise remains
  separately configurable.
- Added Runtime `MixSignals` over read-only `SampleBlockView`s. It requires one
  channel per input and matching sample rate, first sample index, epoch, length
  and unit; it creates an owned output block, propagates discontinuity and
  rejects non-finite sums. This keeps alignment and unit checks with the
  reusable data operation rather than the UI.
- Signal Lab now selects two generated signals for Mix and Phase views. Mix
  displays both inputs plus their sample-by-sample sum and measurements. Phase
  displays the Lissajous trace X=A[n], Y=B[n], with matched sample tooltips and
  configured start-phase difference. Pair results are cached on source/pair
  changes, not recomputed each frame. Hidden inputs are clearly reported.
- Added unit fixtures for exact sawtooth/triangle samples, block-partition
  continuity, aligned mix output/RMS, invalid sample clock and unit, expected
  harmonic amplitudes, and sawtooth/triangle harmonic aliases above Nyquist.
  The harmonic fixture uses 256 samples per cycle so the discrete spectrum
  approaches the continuous Fourier-series values within the stated tolerance.
- `cmake --build build --config Debug --target SignalUnitTest SignalLabMode`
  passed. `ctest --test-dir build -C Debug -L signal --output-on-failure`
  passed 53/53. The Debug `KimPeanutEngine` target then built successfully.
- Visible OpenGL Signal Lab inspection confirmed both new waveform choices and
  their naive/unbandlimited model labels, the three-trace pair mix with legend,
  and the phase plot with short X/Y voltage axes and sample readout. The pair
  selectors use the first two generated blocks by default and remain editable.
- Ownership remains split along the concrete operation boundary: Runtime
  `Signal` owns waveform formulas and aligned block mixing; Signal Lab owns
  pair selection, readout and drawing. No new dependency or GPU lifetime was
  added. Filtering, additional band-limited generation, and captured two-input
  comparison remain future work.
