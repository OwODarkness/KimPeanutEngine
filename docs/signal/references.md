# Signal source study and numerical references

Study date: 2026-11-08. Design question: how can a learning-oriented numerical
module publish trustworthy instrument results without coupling its algorithms
to audio callbacks, UI frames, or a particular GPU API?

See the [engine reference index](../engine-reference/README.md),
[architecture](PLANS.md) and [roadmap](TODO.md). These are pattern studies and
mathematical references; no upstream source is copied or vendored.

## Godot: spectrum state belongs to an effect instance

Inspected actual `godotengine/godot` master source resolved by GitHub to commit
`c51ff06e8836a70ea905701c081bf94124dcd6ce`:

- [`servers/audio/effects/audio_effect_spectrum_analyzer.h`](https://github.com/godotengine/godot/blob/c51ff06e8836a70ea905701c081bf94124dcd6ce/servers/audio/effects/audio_effect_spectrum_analyzer.h)
- [`servers/audio/effects/audio_effect_spectrum_analyzer.cpp`](https://github.com/godotengine/godot/blob/c51ff06e8836a70ea905701c081bf94124dcd6ce/servers/audio/effects/audio_effect_spectrum_analyzer.cpp)

The instance owns temporal FFT buffers, write position, spectrum history and
mix rate. `process` passes audio through while accumulating windowed input,
then transforms a completed window and writes a history slot. Frequency-range
queries reduce magnitude bins into average or maximum. Configuration lives
in the effect resource, while per-stream processing state lives in its instance.
The shown implementation computes FFT work inside the audio process method;
it uses float buffers and magnitude-oriented results rather than a lab-grade
complex-spectrum and PSD contract.

Adopt the separation of configuration, per-stream state and queryable results.
Here the Runtime Signal subsystem owns FFT/filter plans and per-run state, SignalLab owns sample
history and acquisition, and the tool reads immutable published snapshots.
Do not infer a sufficient cross-thread publication protocol from a history
index: KimPeanut must explicitly synchronize ownership and reuse. Do not put
the learning FFT or GPU waits into the Audio callback. Any future live tap only
copies to preallocated bounded storage and exposes overflow/gap counts.

Godot's built-in analyzer illustrates an audio visualization consumer; it is
not a correctness oracle for Signal amplitude, PSD, phase, timing or tolerance.

## FFTW: transform conventions are part of the contract

[Official 1D DFT definition](https://www.fftw.org/fftw3_doc/The-1d-Discrete-Fourier-Transform-_0028DFT_0029.html)
describes forward negative-exponent ordering and unnormalized transforms in
both directions. Signal adopts that forward definition but normalizes its
inverse by 1/N. Any offline FFTW fixture must apply the explicit normalization
before comparison. This is a reference for conventions, not a dependency or
an invitation to replace the educational implementation.

## SciPy: distinguish tone amplitude from noise density

[Official periodogram documentation](https://docs.scipy.org/doc/scipy/reference/generated/scipy.signal.periodogram.html)
distinguishes squared-amplitude spectrum from power spectral density and
documents one-sided combination and the window-dependent scaling ratio.
Use it to cross-check PSD units and window-energy normalization; configure
detrending/window definitions explicitly when generating offline fixtures.
Signal's peak tone-amplitude view is a separately defined convention.

[Official signal-processing tutorial](https://docs.scipy.org/doc/scipy/tutorial/signal.html)
provides convolution/filtering definitions and boundary conventions useful for
independent fixture generation. SciPy remains an optional offline comparison
tool. Analytic impulse, DC, sine, alternating-sign and transfer-function checks
must still catch errors independently of another implementation.

## Mapping to this repository

The local Audio Player already computes a small radix-2 FFT in `SampleSignal`,
but discards phase and clamps a sparse display spectrum. Its waveform bins
store absolute peaks. Keep those presentation helpers local until a separate
migration has a clear consumer and acceptance test. Signal needs signed raw
samples, units, full spectra and explicit timing; moving the existing helper
unchanged would establish the wrong domain contract.

The current common RHI has no compute pipeline/dispatch contract. Godot's CPU
effect does not resolve that engine-specific boundary. SG7 requires a fresh
source study of live Graphics owners, then common compute capability, hazards,
completion and retirement backed by a batched FIR consumer. Queue overlap is
a later measured optimization, not a design assumption. This plan does not
claim that a GPU reference implementation or async queue path was validated.
