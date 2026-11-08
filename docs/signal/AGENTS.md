# Signal module agent guide

Follow the [project contract](../../AGENTS.md). Read [PLANS.md](PLANS.md),
[TODO.md](TODO.md), and the relevant stage design before implementation.
The [execution spec](../../.spec/specs/signal-processing-lab.md) defines scope;
the [planning journal](../../.spec/journal/2026-11-08-signal-processing-plan.md)
records evidence and limitations.

- Canonical module name: `Signal`, source directory `engine/module/signal`,
  proposed namespace `kpengine::signal`. All implementation paths and targets
  in these documents are proposals until their stage lands.
- Implement the learning algorithms directly in C++. OpenCV, FFTW, SciPy and
  GPU DSP libraries are not production dependencies of the initial module.
  External tools may generate independently checked test fixtures offline.
- Keep CPU algorithms headless and independent of Audio, Asset, Editor,
  Render, Graphics, frame rate, wall clock, and device availability.
- Preserve sample rate, absolute sample position, channel identity, units,
  transform normalization, filter delay, and discontinuity information.
- UI receives immutable snapshots and emits intents. It does not mutate a
  running algorithm or own acquisition/filter history. Never measure a
  display-reduced waveform as though it were the original sample buffer.
- Register the host by its application ID in the global
  `ApplicationHostRegistry`; declare its scene/shared-window/owned-window layout
  and gameplay tick policy there. Do not add a fixed Runtime mode slot or
  expand RuntimeLib's dependency on EditorLib.
- Keep signal processing separate from RenderGraph policy. GPU work uses a
  future common compute contract; no Vulkan/OpenGL types in Signal headers.
- Use Level 2 numerical/streaming tests, Level 3 visible GUI checks for lab
  changes, and the project's escalation rules for common headers/CMake/RHI.
  Debug Vulkan correctness includes validation. Performance uses a freshly
  built RelWithDebInfo executable and matched conditions.
- Launch GUI validation outside the sandbox on the Default desktop. Use the
  Runtime command registry and capture service; preserve other tasks' captures.
- Record baseline, fixtures, algorithm version, tolerances, skipped checks,
  ownership decisions, and benchmark conditions. A plausible plot, inverse
  transform round trip, or fast kernel alone does not establish correctness.
