# Signal processing and oscilloscope lab

**Status: SG1–SG3 accepted; SG4 filtering is next (2026-11-08).**
The always-available `Signal` target owns channel-major `SampleBlock` storage,
constant/impulse/sine/seeded-uniform generators, periodic harmonic distortion,
additive seeded noise and scalar measurements; the
standalone `signal-lab` ImGui shell is optional. The bounded scalar complex DFT,
radix-2 FFT, periodic Hann/rectangular windows and one-sided amplitude/PSD
measurements and sample-aligned two-signal mixing live beside these operations.
SG3 includes alias/leakage fixtures, a selected-signal Spectrum view, naive
sawtooth/triangle sources, an A+B mix view and an XY/Lissajous phase view.
Start from
[SG1](.plan/SG1.md), then [SG2](.plan/SG2.md). The [roadmap](TODO.md) owns
stage acceptance; [references.md](references.md) records source studies;
the [spec](../../.spec/specs/signal-processing-lab.md) owns execution scope.

## Purpose and learning loop

Build a C++20 signal-processing laboratory with an oscilloscope (示波器),
function generator, spectrum analyzer, and eventually filtering and
time-frequency experiments. The instrument makes a mathematical prediction
observable: predict -> generate -> process -> measure -> compare -> explain.
Each experiment stores its parameters, seed, expected result, actual error,
and a short explanation. Replaying an experiment should reproduce its data
without relying on a screenshot or remembering an equation.

Use `Signal` / `signal` as the canonical spelling of the Runtime subsystem;
the proposed tool mode is `signal-lab`. Begin with one-dimensional uniformly
sampled signals. Initial scope is numerical DSP and a simulated instrument,
not an analog circuit solver, calibrated hardware oscilloscope, DAQ driver,
or OpenCV/image-processing wrapper. Image convolution and 2D FFT can follow
once the 1D implementation and its performance evidence are established.

## Ownership and dependencies

Introduce targets only when the corresponding stage has a consumer:

| Proposed area / target | Owns | Dependencies |
| --- | --- | --- |
| `engine/runtime/signal` / `Signal` | Sample values, generator state, scalar reference algorithms, FFT plans, filter state; public engine API | C++ standard library; narrowly justified Core/math use only |
| `engine/module/signal/lab` / `SignalLab` | Experiment configuration, acquisition clock, capture/trigger history, session lifecycle, bounded jobs, snapshots | Signal; existing CPU infrastructure where suitable |
| `engine/module/signal/tools` / `SignalLabMode` | Host, command adapter, serialization, ImGui panels and plot presentation | SignalLab, RuntimeCommand, RuntimeLib, EditorUILib; file adapters only as introduced |
| `engine/module/signal/adapters` / later adapter target | Audio/file conversion with explicit units, channel selection and sample-rate conversion | Signal + existing Audio/Asset APIs |
| `engine/module/signal/gpu` / later `SignalGpu` | DSP kernel choice, dispatch descriptions and in-flight job records | Signal + common Graphics/RHI; host schedules work on the graphics owner thread |

Signal is a headless Runtime subsystem, independent of the optional lab UI.
SignalLabMode is an application composition target and never a dependency of
RuntimeLib. Runtime exposes the headless Signal API; the lab consumes it. Keep
SignalLab CPU-only; GPU results enter it through immutable values managed by
the tool integration.
Graphics creates, synchronizes and retires native GPU objects. SignalGpu tracks
which job uses logical resource handles; it never destroys submitted resources
early or creates its own Vulkan device/queue behind the engine.

The first implementation builds the headless `Signal` Runtime target in every
engine configuration. `KPENGINE_ENABLE_SIGNAL` (default OFF) gates only the
standalone lab module. `SignalUnitTest` must test the numerical target without
initializing a window, audio device, RenderSystem, or GPU. A fully graphics-free
top-level CMake configuration is a separate concern, not assumed to exist.

## Existing owners and reuse decision

The operation is reproducible generation, analysis, and instrument acquisition
of sampled signals. Acceptance requires independently verified numerical
results and stable instrument semantics under different UI frame rates.

- Audio Player's `BuildWaveform` and `SampleSignal` in
  [audio_player_controller.cpp](../../engine/module/audio/audio_player_controller.cpp)
  were inspected. The first reduces absolute peaks for display; the second
  performs a 256-point FFT, mixes channels to mono, and scales/clamps 48 bins
  for presentation. These are real existing algorithms, but neither is a
  calibrated general DSP contract. Keep them local to Audio Player initially.
  Do not copy their scaling into Signal or use their output as an oracle.
  A later separate migration can replace Audio Player's numerical analysis
  with Signal only after parity and dependency checks.
- Reuse [module registration](../../engine/module/module_registration.h),
  [application hosts](../../engine/runtime/host/application_host.h),
  [CommandRegistry](../../engine/runtime/command/command_registry.h), and
  EditorUILib presentation/docking. Audio Player and Terrain are concrete
  composition examples; their controllers are not general lab sessions.
- Core's [AsyncQueue](../../engine/runtime/core/async/async_queue.h) is a
  mutex/deque FIFO with no capacity policy. It is suitable only for ordinary
  bounded-by-caller control work, not an allocation-free audio callback. SG2
  needs no worker; SG5 chooses bounded transport for its actual producer.
- Common [CommandRecorder](../../engine/runtime/graphics/backend/common/command_recorder.h)
  has buffer-usage declarations and ray dispatch, but no general compute
  pipeline/dispatch API. SG7 must establish that capability with a real DSP
  consumer; existing ray-tracing storage bindings are not a compute API.

Signal is a focused Runtime domain owner because its numerical invariants and
consumers differ from audio playback and rendering. Trigger history belongs
to SignalLab, which owns acquisition; plot geometry stays local to the lab UI.
Do not create a generic graph framework, utility bucket, or shared mutable
session bag. Initial processing is a typed, ordered chain with validated
sample rates and channels. A node canvas is optional after useful chains exist.

## Data and time contract

```text
ExperimentConfig + source state + integer sample clock
  -> immutable SampleBlock -> optional ordered processing chain
  -> full-resolution measurements / trigger capture / spectrum
  -> immutable LabSnapshot -> scope and spectrum display
```

- A sample block carries `sample_rate_hz`, `first_sample_index` (uint64),
  channel descriptors, count, revision and discontinuity marker. Use a single
  contiguous channel-major `double` buffer and expose each channel as a span;
  this keeps per-channel iteration contiguous without one allocation per
  channel.
- `SampleBlock::View()` returns a read-only borrowed `SampleBlockView`; its
  spans cannot mutate the block and do not copy samples. The block must stay
  alive and unchanged while the view is used. Cross-thread publication later
  needs immutable ownership or an explicit snapshot handoff.
  Complex values appear in transforms/IQ experiments, not as an initial
  universal sample container. Validate finite inputs and bounded allocation.
- Time within an epoch is `t = sample_index / sample_rate_hz`. Generate a
  requested integer count; do not integrate `delta_time` to advance signal
  phase. Keep oscillator phase bounded with an explicit update rule. A change
  of sample rate starts a new epoch; reset or explicitly migrate state.
- A composite source adds normalized second/third harmonic components to its
  periodic base and deterministic uniform noise with an explicit RMS level.
  Its seed and parameters are part of `SignalGeneratorConfig`, so generated
  blocks and scope acquisition reproduce the same source model.
- Generic samples start as dimensionless quantities. A synthetic voltage
  source may declare volts; audio PCM is normalized full-scale data. Never
  silently relabel normalized audio as volts. Frequency uses Hz, phase radians,
  time seconds, and density sample-unit squared per Hz.
- Separate parameter revision, acquisition epoch, and presentation revision.
  A source/filter edit starts a reproducible new run initially. Panning or
  changing plot color must not regenerate or modify acquired samples.
- Offline/step mode is authoritative. Interactive simulation adds sample
  credit from a monotonic clock but processes whole fixed-size blocks with a
  retained fractional remainder. Cap per-tick work; on overload show lag or
  pause, never silently change sample rate, invent samples, or reset filters.
  UI FPS is a presentation measurement, not the acquisition rate.

## Numerical conventions and correctness gates

Write a slow, clear scalar reference first. Each accelerated path must match
that reference and independent analytic fixtures; retain reference access in
tests and experiments. Round trips alone can conceal matching forward/inverse
bugs. The [reference notes](references.md) distinguish external conventions
from this module's explicit choices.

- DFT: `X[k] = sum(x[n] * exp(-i*2*pi*k*n/N))`; inverse has `+i` and `1/N`.
  Frequency bins are `k*fs/N`. Preserve complex phase and negative frequencies;
  real-input one-sided output contains `floor(N/2)+1` bins.
- SG3 adds power-of-two radix-2 FFT; other sizes use the small bounded DFT or
  return an explicit unsupported-size error. Never silently pad/change the
  acquisition window. Zero padding is an explicit analysis operation and
  changes bin spacing, not the resolving power of the original observation.
- Windowed on-bin peak amplitude uses `d[k]*abs(Xw[k])/sum(w)`. PSD uses
  `d[k]*abs(Xw[k])^2/(fs*sum(w*w))`. `d=2` for real one-sided interior bins,
  and `d=1` for DC and the even-length Nyquist endpoint. Use `Nfft` for bin
  spacing and the actual observed window for both sums when padding.
  Coherent-gain amplitude correction is an on-bin tone convention, not an
  unbiased estimate for arbitrary off-bin tones or noise. Detrending defaults
  OFF so a DC experiment retains DC.
- Specify periodic versus symmetric windows; SG3 spectral Hann is periodic
  (`0.5 - 0.5*cos(2*pi*n/N)`, N>=2). Filter-design windows are separately named.
  Window energy and coherent gain are different normalizations.
- Display linear amplitude, power or PSD with explicit labels. Amplitude dB
  uses `20*log10(A/Aref)`; power/density dB uses `10*log10(P/Pref)` with stated
  references. Define dBFS peak convention and finite display floor; zero is
  preserved numerically. Do not call generic relative dB “dBV”.
- Filter contracts specify coefficients, initial state, output length,
  boundary padding, delay, discontinuity/reset policy, and non-finite behavior.
  Independent blocks must produce the same output as one contiguous run when
  carrying the same causal state. IIR chains preserve dependency along time;
  parallelize independent channels/runs first.
- Error checks combine absolute and relative tolerances; normalized L2 checks
  need a nonzero denominator floor. SG1/SG3 define bounded fp64 fixture budgets;
  later fp32/GPU tolerances must be declared per operation and length before
  benchmarking. No bitwise CPU/GPU identity promise or blanket tolerance.

## Algorithms to understand and implement

| Operation | Mechanism to implement directly | Cost / state to inspect |
| --- | --- | --- |
| Oscillator | Phase accumulation, wrapping and sample-index timing | O(N) per channel; persistent phase |
| DFT -> FFT | Complex dot products, then bit reversal, twiddle factors and radix-2 butterflies | O(N²) -> O(N log N); immutable plan plus per-run scratch |
| Convolution -> FIR | Weighted sum of current/past samples, then streaming delay line | O(NM); M-1 history samples for M taps |
| IIR | Difference equation with feedback and explicit pole/stability analysis | O(N) for fixed order; persistent delay state |
| STFT | Window extraction, hop scheduling and repeated transforms | O(F N log N) for F frames; overlap history |
| Decimation | Anti-alias low-pass followed by sample selection | Filter state and phase persist across blocks |
| GPU FIR / FFT | Independent output work, memory tiling, workgroup limits, staged dependencies | Parallel arithmetic plus bandwidth, dispatch and transfer costs |

For every operation, first show its equation, then an index-by-index small
example, then the implementation, then measured output. A filter experiment
must explain why a result changes (gain, phase, delay or transient), rather
than treating the graph as proof. Keep the slow reference simple enough to
read beside the equation after the fast path exists.

## Instruments and workspace

The learner's top tasks are generating a known waveform, comparing a prediction
to a measured capture, and changing one parameter to explain the difference.
Reuse the existing ImGui dock host, theme metrics, font/DPI owner, and input
capture. No independent UI backend or new plotting dependency is needed for
the first scope; clipped draw-list geometry is appropriate for traces.

| Region | Primary content |
| --- | --- |
| Top controls | Run/Pause, Step Block, Reset, experiment preset, capture state |
| Left inspector | Source parameters; sample rate and block size; processing chain later |
| Center scope | Time/amplitude axes, two traces, trigger marker, cursors, fit and zoom |
| SG3 views | Selected-signal spectrum with amplitude/PSD controls; pair mix A+B; XY/Lissajous phase plot with selectable, sample-aligned inputs |
| Right readouts | Mean, RMS, min/max, peak-to-peak, cursor deltas; frequency validity |
| Bottom evidence/status | Expected vs measured error, acquisition count, epoch, gaps, lag; later CPU/GPU timing |

SG2 has a single-source capture and trigger. SG3 mixes generated sample blocks
and compares their phase; captured channel B plus filter comparisons follow in
SG4. Acquisition ring and pre/post-trigger records belong
to SignalLab; UI only selects views. Trigger supports rising/falling edge,
threshold, hysteresis and holdoff; auto/normal/single behavior is defined in
[SG2](.plan/SG2.md). Horizontal labels use seconds/division and vertical labels
use unit/division. Claim instrument bandwidth only if an analog/front-end
model exists; sampled synthetic data has no physical probe calibration.

At close zoom draw raw samples with optional markers. At wide zoom use ordered
min/max envelopes per pixel and disclose display reduction. Measurements,
FFT, export, and triggers always use original samples. Do not draw lines across
known sample gaps or imply that interpolation repairs acquisition aliasing.

| State | Required behavior |
| --- | --- |
| Empty | Offer the default sine experiment and Step Block; no fake zero measurements |
| Running / paused | Show sample position and acquisition state; paused navigation retains capture |
| Waiting for trigger | Retain last complete trace with age/epoch; show armed state |
| Invalid parameters | Inline diagnostic; keep the last valid capture visibly marked stale |
| Overload / gap | Show lag or dropped count; break the trace and reset affected analysis history |
| Unsupported GPU | Show CPU fallback and reason; never label it GPU |
| Completed capture | Retain immutable capture, parameters and numeric comparison |

Use stable experiment/channel IDs; color plus labels distinguish traces.
Keyboard navigation reaches parameters and transport. Space toggles run when
the lab owns keyboard input and no text edit is active; an explicit Step button
is always available. Dragging cursors has a visible hit area, clipping and
Escape cancel. Reset starts a new run without discarding saved experiments.
Verify narrow/wide layouts and DPI before UI acceptance. Session/experiment
JSON schema belongs to the tool adapter; numerical code does not depend on JSON.

## Correctness before performance

SG1-SG5 establish CPU experiments and stream contracts. SG6 measures and
optimizes CPU memory layout, allocation, SIMD and independent jobs. SG7 is the
first GPU stage and starts with a small FIR batch on the existing graphics
queue; SG8 evaluates larger FFT/convolution batches and residency.

GPU parallel execution and overlap across queues are separate capabilities.
One GPU dispatch already has parallel work. Add async compute only after
evidence shows useful overlap, including queue ownership, dependencies,
render contention and total latency. More streams/queues do not establish a
speedup. Use explicit capability reporting and CPU fallback on either API.

The GPU adapter owns in-flight slots with epoch/revision tags. At completion,
publish immutable results only if still current; stale jobs retire safely.
Reuse staging/device buffers only after the completion primitive proves safety.
Cancellation prevents publication; it does not free submitted work. Shutdown
stops admission, drains CPU jobs, retires submitted GPU resources through
Graphics, and then allows Graphics teardown. Test resize, cancellation,
parameter edits, device failure and shutdown while work is pending.

Performance comparisons must record working-tree/commit state, compiler and
configuration, API/validation, CPU/GPU/driver, algorithm and precision, source
seed, N/channels/batch/filter length, viewport/RT mode when a GUI is running,
warm-up, residency, sample window and input/output transfers. Use matched
RelWithDebInfo runs with no builds/tests in parallel. Report p50/p95 latency,
throughput, allocations/memory, numerical error and actual selected backend.
Separate kernel-only, CPU->GPU->CPU end-to-end, and resident chained workloads.
Choose GPU only above a measured crossover; a small interactive scope may
remain faster on CPU. No GPU speed target is claimed before measurements.
