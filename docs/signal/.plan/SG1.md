# SG1 — sampled-signal foundation

**Status: complete (2026-11-08).** Parent: [roadmap](../TODO.md). Boundary and numerical
conventions: [architecture](../PLANS.md). Deliver a headless reference library
before the graphical instrument. No worker, Audio integration, GPU or graph UI.

## Concrete question and deliverable

Can one generate and measure exactly the same signal regardless of how sample
requests are partitioned, with correct units and independently checked values?
The deliverable is the Runtime `Signal` target + SignalUnitTest and small checked-in numeric
fixtures; SG2 supplies the visible experiment.

## Proposed source placement

```text
engine/runtime/signal/CMakeLists.txt
engine/runtime/signal/sample_block.h
engine/runtime/signal/signal_generator.h/.cpp
engine/runtime/signal/analysis/signal_statistics.h/.cpp
engine/runtime/signal/fixtures/     # bounded canonical fixture data/parameters
engine/module/signal/CMakeLists.txt # optional Signal Lab host/editor only
engine/test/unit/signal/CMakeLists.txt
engine/test/unit/signal/*_test.cpp
```

Keep schema close to generation/analysis. No generic `signal_utils.h` or Core
DSP extraction is needed. Reuse standard containers, spans, complex numbers,
and existing GoogleTest discovery/labels; depend on existing engine types only
when that avoids a real duplicate contract.

## Minimal operations and ownership

- `SampleBlock`: one contiguous channel-major double buffer + immutable
  metadata once published, exposed as per-channel spans. One channel initially,
  explicit units, finite positive fs, count,
  first index and epoch. Check count multiplication/index addition overflow
  before allocation; proposed initial limits are 8 channels and 1,048,576
  total samples per block. Reject invalid/oversized requests diagnostically.
- `SignalGeneratorConfig`: kind, sample rate and unit plus source parameters.
  Constant uses `constant_value`; impulse emits `offset + amplitude` at the
  configured run sample index and `offset` elsewhere; sine uses amplitude,
  offset, frequency and phase; uniform noise uses offset and amplitude as the
  center and half-range. Frequency may exceed Nyquist for aliasing experiments.
- `SignalGenerator`: owns epoch, next sample index, bounded oscillator phase
  and deterministic PRNG state. Each requested sample advances once; an empty
  request changes no state. Reset increments epoch, returns sample index to
  zero, restores the configured phase and seed. Parameter edits create a new
  generator in SG1; phase-continuous modulation is later work.
- Noise uses SplitMix64 modulo-2^64 arithmetic: increment state by
  `0x9E3779B97F4A7C15`, mix with shifts 30/27/31 and multipliers
  `0xBF58476D1CE4E5B9` and `0x94D049BB133111EB`, then map the top 53 bits to
  `u = bits * 2^-53` and output `offset + amplitude * (2*u - 1)`. Seed zero is
  valid. This fixes both the integer sequence and double mapping; do not use
  `std::uniform_real_distribution` for a golden sequence.
- `Measure(samples)`: count, mean, RMS, min, max, peak-to-peak; use double
  accumulation and define numerical limits. Empty input returns unavailable,
  not a fabricated zero measurement. Reject NaN/Inf explicitly.

Use ordinary value/result types; do not freeze these tentative operation names
as a public engine-wide API before implementation. The caller owns source
state; each independent run gets its own state and sample storage.

## Independent fixtures and acceptance

| Input / action | Expected result |
| --- | --- |
| Constant 2, N=64 | mean=2, RMS=2, min=max=2, peak-to-peak=0 |
| Unit impulse in N=64 | mean=1/64, RMS=1/8, peak=1 |
| [1,-1,1,-1] | mean=0, RMS=1, min=-1, max=1 |
| Sine A=1, f=1 kHz, fs=48 kHz, phase=0, N=4800 | 100 cycles, mean≈0, RMS≈sqrt(1/2), min≈-1, max≈1 |
| Quarter-cycle samples at fs=4*f | [0,1,0,-1] within fp64 tolerance |
| SplitMix64 seed=0, A=1, offset=0 | `[0.7666216164272852, -0.13694400590298006, -0.9471324568148045, 0.941763956307657, -0.7873066168655751, -0.3453484715637485, -0.6522642680806343, 0.543093112663134]` |
| Same noise seed/config, reset | Same noise sequence; different partition sizes preserve it |
| Counts 1,17,255,0,4096 vs one total request | Same sample indices, generated values and final state |
| Invalid fs, NaN, oversized block, index overflow | Explicit failure; no partial publication/state advance |

For the listed bounded fixtures use absolute error <=1e-10 for analytic samples
and statistics. Partitioned execution must produce identical values in one
build because per-sample state updates are identical. Cross-toolchain
transcendental results use tolerances, not byte identity. Do not infer broadband
noise quality solely from deterministic replay; noise distribution tests later
use declared finite-sample statistical bounds.

Fixture expectations come from hand-derived values or independently generated
offline data with tool/version/convention provenance. Never generate both
expected and actual results with the production function under test.

## Wiring and validation

1. [x] Inspect existing module/test options and ownership evidence again.
2. [x] Add the optional switch and numerical targets; leave acquisition for SG2.
3. [x] Keep the Runtime target headless and link Signal Lab only through its
   optional module target.
4. [x] Build Debug and run focused signal CTest coverage using the existing test registration
   conventions:

```powershell
cmake -S . -B build -DKPENGINE_ENABLE_SIGNAL=ON
cmake --build build --config Debug --target SignalUnitTest
ctest --test-dir build -C Debug -L signal --output-on-failure
```

Check that tests are actually discovered under `signal`; do not assume the
target name is a CTest name. The seed-zero golden vector was generated by an
independent Python integer-arithmetic reference using the constants and mapping
above. SG1 acceptance covers it alongside analytic constant, impulse and sine
fixtures, block partitioning, empty calls, reset/replay, bounded allocation and
configuration failures.
