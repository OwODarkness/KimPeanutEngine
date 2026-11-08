# Signal roadmap and acceptance ledger

**SG1 complete; SG2 implementation in progress and acceptance remains.** [Architecture](PLANS.md) · [Local rules](AGENTS.md) ·
[Spec](../../.spec/specs/signal-processing-lab.md) ·
[Planning evidence](../../.spec/journal/2026-11-08-signal-processing-plan.md)

Each stage delivers an experiment, its underlying implementation, and numeric
evidence. Complete correctness gates before optimizing that operation.

## Bootstrap shell (complete)

- [x] Add an always-available headless Runtime `Signal` target and planar
  `SampleBlock` metadata contract; keep standalone `signal-lab` as an optional
  module behind `KPENGINE_ENABLE_SIGNAL`.
- [x] Register optional application hosts by module-owned ID in the global
  registry; Runtime provides the built-in `scene3d` registration without a
  fixed host-table slot or enum case.
- [x] Complete SG1 source generation/statistics and display a generated sine
  block in Signal Lab; proceed to SG2 acquisition.
- [x] Expand Signal Lab into a bounded multi-signal workspace: generate
  sine/cosine/square blocks, select entries by name/color, show/hide traces,
  inspect details, and switch between colored overlays and their summed
  time-domain composite. DFT spectrum remains a separate SG3 operation.

| ID | Learn by implementing | Visible experiment / acceptance |
| --- | --- | --- |
| SG1 | Sample clock, units, phase, scalar generators and statistics | Headless 1 kHz sine at 48 kHz; analytic samples and RMS checked; same samples across block partitions. [Design](.plan/SG1.md) |
| SG2 | Acquisition ring, triggering, timebase, display sampling | Dedicated scope with Run/Pause/Step and stable triggered capture; same acquisition at different UI FPS. [Design](.plan/SG2.md) |
| SG3 | Complex arithmetic, direct DFT, radix-2 FFT, windows, spectra | Predict alias frequency and leakage; on-bin/off-bin tone, two-tone, impulse, DC, Nyquist and noise experiments pass independent normalization checks |
| SG4 | LTI systems, convolution, FIR and IIR state | Remove high-frequency noise and explain delay/ringing; direct convolution and causal filters pass impulse, step, frequency-response and block-state checks |
| SG5 | STFT, overlap, resampling and stream continuity | Chirp spectrogram and anti-aliased downsampling; bounded acquisition under UI stalls; saved/imported experiments reproduce results |
| SG6 | Data layout, profiling, SIMD, CPU concurrency | Measured reference vs optimized CPU comparison with unchanged numerical meaning and explicit error budgets |
| SG7 | Compute kernels, buffers, workgroups and synchronization | Batched FIR through common RHI on Vulkan, with capability-aware CPU fallback; numerical, validation and lifetime gates before timing |
| SG8 | GPU FFT/convolution, residency and optional overlap | Batched spectra or long FIR benchmark establishes crossover and transfer cost; async compute only if total latency/throughput improve |

## SG1 — headless foundation

- [x] Add headless SignalUnitTest coverage without UI/GPU links; keep the Runtime
  Signal target independently usable without window, audio or GPU setup.
- [x] Define validated contiguous channel-major samples, epoch/sample index,
  units and reset policy.
- [x] Implement constant, impulse, sine and seeded uniform noise; mean/RMS/min/max.
- [x] Pass independent fixtures, the SplitMix64 golden sequence, partition
  invariance, reset replay, empty-call and failure-without-state-advance checks in
  [SG1](.plan/SG1.md).

## SG2 — first useful oscilloscope

- [x] Generate reproducible composite sources from a periodic base, normalized
  second/third harmonics, and seeded additive noise; feed the same source
  configuration to the sample list and Scope.
- [ ] Verify launch-help and mode isolation through the global host registry.
- [x] Implement bounded acquisition and auto/normal/single triggers across blocks.
- [x] Show source/timebase/trigger controls, units, readouts and capture validity.
- [ ] Verify visible Vulkan/OpenGL captures, pause/step/reset, resize, DPI and
  orderly shutdown; headless correctness remains independently runnable.

## SG3 — frequency and sampling experiments

- [ ] Write scalar complex DFT/inverse with sign, ordering and normalization tests.
- [ ] Implement radix-2 FFT and compare every complex bin against DFT for
  N=8,16,32,64,128,256 and seeded inputs. Initial fp64 target: normalized L2
  <=1e-10; max error <=1e-10*max(1,N*max_abs_input) for these bounded fixtures.
- [ ] Add periodic Hann/rectangular windows, coherent-gain amplitude and PSD;
  verify DC/Nyquist endpoint handling and Parseval independently of round trip.
- [ ] Check a 1 V-peak on-bin tone at 48 kHz/N=4800 has 1 V amplitude and
  0.5 V² integrated rectangular-window PSD (DFT oracle may use smaller N).
  Use a coherent power-of-two fixture for FFT and compare both conventions.
- [ ] Demonstrate 7 kHz sampled at 8 kHz appearing at 1 kHz; contrast deliberate
  aliasing with the generator's optional band-limited sources added later.
- [ ] Add square, saw and triangle sources with explicit naive/unbandlimited
  labels; inspect harmonics and aliasing. Add two-source mixing and phase view.
- [ ] Explain off-bin leakage and window tradeoffs; zero padding must not be
  presented as improved observation resolution. Save expected/measured results.

## SG4 — filtering and systems

- [ ] Implement full direct convolution (length Nx+Nh-1), causal FIR with
  explicit zero initial state, and windowed-sinc low-pass coefficients.
- [ ] Implement a first-order low-pass, then a stable biquad with normalized
  coefficient/sign convention and double-precision state. Validate poles.
- [ ] Compare impulse/step and sine-sweep response to direct transfer-function
  evaluation; check DC gain and FIR group delay (M-1)/2 for symmetric M taps.
- [ ] Verify chunked vs contiguous output; do not parallelize IIR samples
  without a mathematically valid state transformation.
- [ ] Scope input/output together; report startup transient, delay and clipping.
  Add correlation/delay estimation as an optional SG4 extension.

## SG5 — streaming and time-frequency

- [ ] Add STFT with explicit frame timestamps, N, hop and window, bounded
  history, and correct frame continuity across arbitrary input blocks.
- [ ] Verify chirp ridge and tone amplitudes against generated ground truth;
  add overlap-add reconstruction only with an explicit normalization condition.
- [ ] Implement integer-factor FIR decimation first. Predict aliased components
  with/without the low-pass, and verify passband/stopband error and rate/delay.
  Rational/polyphase resampling follows only after this acceptance.
- [ ] Add one bounded simulation worker when profiling justifies it; transport
  exposes queue capacity, lag/gaps, cancellation and stale-revision rejection.
- [ ] Add versioned experiment JSON and bounded CSV/WAV adapters; reuse existing
  decoding/import and Audio playback paths instead of adding codecs/devices.
  File input remains optional. Microphone/hardware capture is deferred.

## SG6 — CPU optimization

- [ ] Profile representative N=256,4096,65536; 1/2/32 channels or independent
  runs; FIR M=15,127,1023; record workload, precision and working-set size.
- [ ] Separate algorithm time, allocation, queueing and plot update cost.
- [ ] Preallocate scratch, cache immutable FFT plans, optimize memory traffic;
  then evaluate SIMD and channel/batch-level jobs against the scalar oracle.
- [ ] Add FFT-based convolution with sufficient zero padding and verified
  overlap-save/add boundaries; measure crossover against direct convolution.
- [ ] Publish matched RelWithDebInfo p50/p95, throughput and error. Preserve a
  selectable reference path; fast-math changes need separate accuracy evidence.

## SG7 — first GPU correctness slice

- [ ] Reinspect live RHI at implementation time. Design compute capability,
  pipeline/bindings, dispatch limits, buffer read/write hazards, upload/readback
  and completion/retirement with Graphics owners; no DSP math in RHI.
- [ ] First kernel is an independent-channel/batched FIR. Each output has the
  same causal input history as the CPU reference; immutable coefficients are
  shared, per-job outputs/scratch do not race.
- [ ] Start on the current graphics queue; no async queue manager prerequisite.
- [ ] Validate workgroup boundaries, odd/short tails, zero work, limits and
  errors against scalar fp64, with declared fp32 size-dependent tolerances.
- [ ] Test edit/cancel/shutdown in flight, out-of-order completion publication,
  failed dispatch, device errors, safe reuse and deferred destruction.
- [ ] Run Debug Vulkan validation plus affected graphics tests/smoke and broad
  validation for shared RHI contracts. Verify CPU fallback on unsupported APIs.
- [ ] Only after correctness, record transfer-inclusive timing and memory.

## SG8 — GPU throughput and residency

- [ ] Implement batched radix-2 FFT or long FIR/convolution, chosen from SG6/7
  bottleneck evidence; compare full complex results and spectrum normalization.
- [ ] Measure single-channel scope latency separately from large independent
  batches. Record queueing, uploads, kernel, readback and total p50/p95.
- [ ] Keep a useful transform/filter chain resident; read back measurements or
  reduced traces only when their consumer requires them. Report this workload
  separately from CPU->GPU->CPU comparisons.
- [ ] Adopt GPU selection only at a measured crossover with CPU fallback.
- [ ] Evaluate queue overlap last; require queue ownership/hazard tests and an
  end-to-end improvement including presentation contention before enabling it.

## Deferred scope and decisions

Hardware acquisition, analog circuit integration, adaptive filters, modulation/
demodulation, wavelets and 2D image processing are later experiment families.
No dates or speedup promises are assigned before the first CPU slice exists.
Select new families from observed learning needs rather than prebuilding APIs.
