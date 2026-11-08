# SG2 — first simulated oscilloscope

**Status: proposed; depends on SG1.** [Roadmap](../TODO.md) ·
[Architecture](../PLANS.md). Deliver a one-source instrument with trustworthy
acquisition, triggering, readouts, and replay. Spectrum begins in SG3.

## Experiment and acceptance boundary

Default experiment: 1 V-peak, 1 kHz sine, 48 kHz sampling, zero offset and phase.
Generate fixed blocks of 256 samples; display a 480-sample (10 ms) capture with
120 pre-trigger samples. Expect 2 V peak-to-peak, RMS sqrt(1/2) V over a whole
number of periods, and 1 ms period. Trigger capture does not have to be aligned
to a block boundary. The first 120 samples prime the pre-trigger history.

## Host integration

Use the Audio Player/Terrain application-host and EditorUILib presentation
pattern. Add `ApplicationMode::SignalLab`, `signal-lab` parser/help text,
module-owned registration and an option-gated host factory. Inspect and update
the fixed-size host factory array, invalid index sentinel, mode-name mappings,
`UsesHostPresentation`, and launch/startup guards together. Test enabled and
disabled mode selection and keep the existing modes working.

Compose SignalLabMode -> SignalLab/SignalCore; RuntimeLib does not link back
to the module. The host can use existing shared presentation initialization
even if it currently requires minimal scene-service setup; do not undertake
a broad host-service extraction solely for this tool.

SG2 uses game-thread simulation and render-thread immutable snapshots, no
worker. The host owns session lifetime. GUI and command mutations pass through
the session owner using the existing command scheduling pattern. The published
snapshot has copied statistics and shared immutable capture storage; render
does not retain a view into a ring the game thread may overwrite.

## Acquisition and trigger semantics

One bounded ring holds raw samples with absolute indices; proposed capacity
65,536 samples. A completed capture owns its samples separately. Clamp capture
length/pre-trigger settings to valid combinations before arming; fail with a
diagnostic if requested history cannot fit. Display reduction has no effect on
this ring or trigger decisions.

- Source clock advances by integer samples; Step Block generates exactly 256
  samples while paused. Reset clears history, trigger state and phase, increments
  epoch, and rearms according to selected mode. Pause retains all source and
  trigger state. Transport runs independently of plot visibility.
- For a rising trigger, arm when a sample is <= level-hysteresis, then fire
  at the first later sample >= level. Falling is symmetric. Hysteresis is a
  nonnegative rearm margin; with zero margin require a strictly directional
  crossing to avoid retriggering a constant signal equal to the level.
- Preserve rearm state and previous sample across generation blocks. Record
  the first qualifying integer sample index; a cursor may later interpolate
  crossing time, but interpolation does not change which samples are captured.
- Require pre-trigger history before accepting an edge. Collect configured
  post-trigger samples before publishing. Holdoff is in integer samples from
  the accepted trigger index; do not rearm until holdoff is over and the next
  capture can be represented without corrupting the completed one.
- Normal mode waits for a qualifying edge, retaining the previous complete
  capture with a stale/age label. Auto mode does the same, but after a
  configurable sample-count timeout publishes the latest complete untriggered
  window marked `Auto / no trigger`; this does not invent an edge. Single
  mode completes one capture, stops acquisition and requires explicit rearm.
- Source/rate edits start a new epoch and clear the acquisition/analysis state.
  Plot pan/zoom/cursor edits do not. On any known gap, invalidate edge history;
  never trigger across missing data.

Estimate frequency from repeated valid crossings in full-resolution samples,
with explicit minimum edge count and uncertainty from sample spacing. Show
unavailable for DC/noisy ambiguous captures instead of a misleading 0 Hz.

## Controls and presentation

Follow the workspace and state matrix in [PLANS.md](../PLANS.md). Implement
transport, source inspector, scope and measurements before secondary panels.
Keep experiment prediction and measured error adjacent to the scope readouts.
Use units/division, numeric axes, fit, pan/zoom, and two time cursors. A sine
curve with raw sample markers teaches the difference between sample points
and the interpolated display line.

At wide zoom create a per-pixel min/max envelope from the selected raw range;
at close zoom use raw samples. Clip geometry to the plot rectangle and reuse
scratch storage. Validate a narrow impulse stays visible after reduction.
Label peak-to-peak/RMS as capture measurements, separate from source amplitude.
No glow/persistence visual effect is needed for SG2 acceptance.

Proposed commands, implemented through CommandRegistry in this stage:

- `signal.configure`: validated source/acquisition parameters; returns revision.
- `signal.run`, `signal.pause`, `signal.reset`: explicit transport state.
- `signal.step`: one block while paused; returns sample position.
- `signal.arm`: explicit single/normal/auto rearm.
- `signal.status`: acquisition position, epoch, trigger, units, capture ID,
  measurements, lag and diagnostics; one consistent snapshot.

Use the existing JSON-lines `execute`/`poll` protocol for game-thread commands.
These names and mode are proposals, not currently available commands. Screenshots
use existing `capture.glfw_window` and poll into
`save/screenshots/validation/signal-sg2-*.png`; never implement a second capture
path or scrape Editor console text to obtain numerical results.

## Required evidence

- Headless trigger tests cover rising/falling, hysteresis, constant-at-threshold,
  cross-block edge, wraparound, startup prehistory, timeout, holdoff, single,
  pause/resume, reset, rate change and gap. Test expected capture indices.
- Replaying 48,000 samples through 1/256/uneven partitions yields equal data
  and trigger indices. Simulated 30/60/144 Hz presentation schedules produce
  equal samples for the same acquired count and never change filter/source fs.
- Default 480-sample capture meets SG1 numerical readouts within its tolerance;
  frequency error is bounded by the documented sample-index estimator.
- Build/test affected mode and Signal targets. CMake/shared mode-header changes
  trigger the appropriate broad validation, not just the new unit test.
- Launch Debug GUI outside sandbox, confirm Default desktop, validation ON for
  Vulkan, and capture the complete workspace on Vulkan and OpenGL. Confirm the
  GLFW30 window and foreground handle before claiming foreground focus.
- Exercise Run/Pause/Step, trigger waiting/single, invalid parameters, resize,
  minimize/restore, narrow/wide layout, keyboard input, DPI and shutdown.
  Capture paths/commands/configuration belong in the execution journal.
- If shared Render/presentation code changes, also perform the project's
  canonical textured-scene regression checks. A GUI capture validates layout;
  numeric status/tests establish signal correctness.
