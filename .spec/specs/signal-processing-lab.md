# Signal processing lab execution contract

- Status: SG1–SG3 accepted; SG4 filtering open
- Owner: Runtime Signal subsystem and Signal Lab module / Amadeus
- Parent TODO: [Signal roadmap](../../docs/signal/TODO.md)
- Architecture: [PLANS.md](../../docs/signal/PLANS.md)
- Planning evidence: [journal](../journal/2026-11-08-signal-processing-plan.md)

## Objective

Learn signal processing through directly implemented C++ algorithms and
reproducible oscilloscope/spectrum/filter experiments. Establish numerical
correctness first, then CPU performance and measured GPU parallel execution.

## Current state

The planning baseline was HEAD `8a86bf1b3cc22014704a932e317b1d8603f4ac73`.
The current working tree adds an always-available headless Runtime `Signal`
subsystem with contiguous channel-major `SampleBlock` storage, deterministic
constant, impulse, sine, cosine, square, naive sawtooth/triangle and seeded
uniform-noise generation, scalar statistics, bounded DFT/FFT, windowed spectrum
measurements, and aligned pair mixing. The optional module provides the
host-presented `signal-lab` ImGui shell with scope, spectrum, pair-mix and phase
views. Application hosts register
descriptors in a process-global registry. Audio Player still owns its local
waveform reduction and small FFT display. Common CommandRecorder still has no
general compute dispatch contract.

## Scope and non-goals

SG1 establishes sampled storage, deterministic source generation and scalar
statistics. SG2 establishes simulated capture; SG3 establishes CPU frequency,
aliasing, mixing and phase experiments. SG4–SG8 filtering, streaming, CPU
optimization and GPU acceptance remain open. Signal Lab generator samples are
static observations; the scope path owns its separately simulated capture.

The planned implementation begins with 1D sampled real CPU signals and a
dedicated simulated scope. Hardware acquisition, analog circuit simulation,
2D computer vision, a generic node editor and GPU queue overlap are deferred.

## Ownership/reuse gate

Operation and acceptance: reproducible sampled-signal generation, analysis
and capture, independent of UI FPS and verified against analytic values.

Candidates inspected: Audio Player `BuildWaveform`/`SampleSignal`, module
registration and host APIs, Panel/Terrain target composition, CommandRegistry,
Core AsyncQueue and common CommandRecorder. Audio display scaling lacks a
general numerical contract; it stays local initially. Reuse registration,
presentation and command infrastructure. Signal is a distinct headless Runtime
domain; SignalLab owns acquisition/experiment lifetime. The optional lab module
depends on Runtime, never the reverse.

## Invariants

- Sample clock, units, epochs, channels and processing state are explicit.
- Channels share one contiguous planar allocation and expose bounded spans;
  importing interleaved audio is an adapter concern.
- `SampleBlockView` is a Runtime-level read-only borrowed view, independent of
  GUI code; the source block remains alive and immutable while observed.
- Scalar reference and analytic fixtures remain available after optimization.
- Display reduction never changes data used for measurement or processing.
- UI consumes immutable values and never owns mutable acquisition history.
- Audio callbacks do not execute unbounded analysis or wait on CPU/GPU work.
- Native GPU resources, synchronization and retirement remain below RHI.
- Cancellation/stale revision prevents publication, not safe resource retirement.
- CPU fallback remains correct and visible; performance includes transfer costs.
- Unrelated user changes are preserved; status additions remain concise.

## Stages

The authoritative acceptance ledger is [TODO.md](../../docs/signal/TODO.md):
SG1 numerical foundation -> SG2 scope -> SG3 spectra -> SG4 filters ->
SG5 streaming/resampling -> SG6 CPU optimization -> SG7 GPU correctness ->
SG8 GPU throughput/residency and evidence-driven overlap.

## Initial implementation note (2026-11-08)

The first implementation slice added the validated `SampleBlock` storage and
borrowed `SampleBlockView` contract to Runtime's always-available `Signal`
target, a deterministic sine source and scalar statistics, and an optional
`signal-lab` module whose ImGui workspace generates and compares multiple
sampled waveforms.
The scope shows a static generated block; SG2 still owns acquisition,
timebase, trigger and live controls.

Operation and acceptance: carry validated contiguous channel-major sample
channels with sample rate, absolute first index, epoch, discontinuity and
per-channel units; generate reproducible sine samples and statistics; expose
the data through a non-copying read-only view and display the samples in the
standalone `signal-lab` host.

Existing candidates inspected: Audio Player's `BuildWaveform` and
`SampleSignal` remain playback-local display analysis; the Audio and TTS host
and `EditorUI::InitializeViewer` flows provide concrete app composition and
presentation patterns. Host lookup is a global registry keyed by application
ID, with Runtime layout and gameplay-tick traits carried in each descriptor.

Placement and reason: keep `engine/runtime/signal/sample_block.h` and the
headless `Signal` target in Runtime so audio and other engine systems can use
the signal data contract without enabling an editor mode. Place the `signal-lab`
host and ImGui presentation under `engine/module/signal/tools` as optional
application composition. Reuse Runtime's application host, global scene
services and Editor UI backend; no Audio, DSP, Graphics or GPU algorithm
dependency is added.

Consumers, ownership/lifetime, validation: producers own construction and
publish completed blocks for read-only consumption. The borrowed view is valid
while its source block remains alive and unchanged. SignalLabHost owns its
editor until render-thread shutdown; Runtime retains window, RenderSystem and
backend lifetime. `KPENGINE_ENABLE_SIGNAL` is OFF by default and gates only
the lab module; the Runtime Signal target is always included. At this first
slice, SignalUnitTest passed 7/7 and visible inspection was pending. The later
SG1 generator follow-up below supersedes that test count and records current
visual evidence.

Host registration follow-up: the fixed `ApplicationMode` enum and per-Engine
factory array were replaced by `ApplicationHostRegistry::Global()`. A module
adds one static `ApplicationHostRegistration` with an ID, factory, composition
layout and gameplay-tick flag; Runtime dispatches from that descriptor. The
launch parser validates ID syntax, while `main` checks availability in the
linked registry. Mode-specific command-line options remain in the parser only
when they name that module's content. New modes no longer require a Runtime
enum case, host table slot, or presentation-mode branch.

## Acceptance for this planning task

- [x] Architecture defines numerical, UI, Audio and Graphics ownership.
- [x] Every stage has an observable experiment and correctness gate.
- [x] SG1/SG2 are concrete enough to implement without a broad refactor.
- [x] External source study and existing-owner investigation are recorded.
- [x] Markdown links/anchors and scoped whitespace pass review.
- [x] Status distinguishes the implemented Runtime scaffold from open
  implementation-stage acceptance.

The spec stays in progress for SG2–SG8; SG1 is accepted. Completion of this
numerical stage does not imply that the planned acquisition-backed oscilloscope
is functional.

## SG1 generator completion (2026-11-08)

The sine-specific generator was consolidated into Runtime `SignalGenerator`
with constant, impulse, sine and seeded uniform-noise kinds. The noise sequence
uses fixed SplitMix64 modulo-2^64 constants and a top-53-bit binary64 mapping;
seed zero, golden samples, reset replay and partition invariance are covered.
Constant and unit-impulse fixtures verify analytic statistics. See the
[SG1 implementation journal](../journal/2026-11-08-signal-sg1-generators.md).

Validation: SG1's Debug `SignalUnitTest` passed 13/13. SG2 implementation now
passes 25/25 direct GoogleTest cases and builds the Debug application. Vulkan
visual and live status checks are recorded in the [SG2 implementation journal](../journal/2026-11-08-signal-sg2.md); OpenGL and lifecycle acceptance remain open.

## Validation plan

Planning uses Level 0: inspect local links/anchors, ownership boundaries,
reference provenance and diff whitespace. Run placement evidence before
editing and during final review. Future numerical work uses Level 2; GUI and
GPU work adds Level 3 and the required broad checks for shared contracts/CMake.
See [validation matrix](../../docs/validation_matrix.md). No C++ tests or
runtime measurements are appropriate to claim from documentation alone.

## Risks and open questions

GPU compute is a real later RHI deliverable. Precision tolerances for optimized
and GPU paths need operation/size-specific budgets; none is accepted yet.
Small scope workloads may favor CPU. Physical voltage units need an explicit
source calibration if hardware arrives. Fully headless top-level CMake setup,
future file formats, live audio capture and advanced experiment families will
be selected only when their stages have concrete consumers.
