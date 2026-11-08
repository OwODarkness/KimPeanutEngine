# Signal Runtime integration — 2026-11-08

## Direction

The user clarified that Signal should be usable by engine systems like Audio,
not exist only inside the optional Signal Lab module. `signal-lab` remains an
optional module; the sample contract and future signal algorithms belong to an
always-available, headless Runtime subsystem.

## Changes

- Moved `SampleBlock` to `engine/runtime/signal/sample_block.h`.
- Stored its channels in one contiguous channel-major `std::vector<double>`;
  `ChannelSamples()` returns a checked span for one channel. This preserves
  sequential per-channel access without allocating one sample vector per
  channel.
- Added Runtime `SampleBlockView`, a borrowed read-only metadata/sample view
  created by a block. It performs no sample copy and is independent of the Lab
  UI; lifetime and no-mutation requirements are documented on the view.
- Added the `Signal` CMake interface target under Runtime and linked it publicly
  through `RuntimeLib`, making its API available to Runtime consumers.
- Changed the optional lab module to consume `Signal`; the
  `KPENGINE_ENABLE_SIGNAL` option gates only the lab host/editor composition.
- Updated the architecture plan, roadmap, and execution spec to record this
  ownership boundary. No generator, analysis operation, Audio integration, or
  callback behavior was added.

## Ownership and dependency direction

Runtime owns sample metadata and will own reusable generation/analysis APIs.
SignalLab owns the instrument workspace and acquisition presentation. The
dependency points from the optional lab module to Runtime; Runtime does not
depend on the lab or Editor UI. The headless Signal target has no Audio, window,
RenderSystem, ImGui, or GPU dependency.

## Validation

- `cmake --build build --config Debug --target KimPeanutEngine`: passed with
  `KPENGINE_ENABLE_SIGNAL=ON`; both Runtime Signal API consumption and optional
  Signal Lab module compile in the linked application.
- Final placement and `git diff --check` results are recorded after those
  checks completed successfully; the scoped placement check passed and
  `git diff --check` reported no whitespace errors.
- Numerical tests and Audio interoperability are not implemented or validated.
