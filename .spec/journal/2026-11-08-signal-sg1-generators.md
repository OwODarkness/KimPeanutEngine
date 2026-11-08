# Signal SG1 generators — 2026-11-08

## Operation and acceptance

Generate constant, impulse, sine and seeded uniform-noise blocks from one
headless Runtime API. Accept only when analytic fixtures, a fixed noise golden
sequence, block partition equivalence, empty calls, reset/replay, and failed
request state preservation pass in `SignalUnitTest`.

## Existing candidates and placement

- `engine/runtime/signal/sine_generator.*` was the existing source owner. Its
  only consumers were Signal Lab and `SignalUnitTest`; it owned sample index,
  epoch, phase and block construction. Replaced it with `SignalGenerator` rather
  than creating parallel source-specific state machines.
- Reused `SampleBlock::Create` for bounded allocation and metadata, and `Measure`
  for scalar acceptance checks. Kept all generation and PRNG state inside the
  headless Runtime `Signal` target; no UI, device, Core RNG or standard-library
  distribution dependency was introduced.
- Signal Lab continues to consume `SignalGenerator` and selects `SignalKind::Sine`
  for its current preview. Acquisition and source controls remain SG2/UI work.

## Deterministic noise contract

Uniform noise uses SplitMix64 modulo-2^64 arithmetic with fixed increment and
mix constants, then converts the high 53 bits to `u` in `[0,1)` using
`2^-53`. Each sample is `offset + amplitude * (2*u - 1)`. Seed zero is valid;
reset restores the configured seed. Golden samples for seed zero, offset zero
and amplitude one were calculated by a separate Python integer-arithmetic
reference and recorded in [SG1](../../docs/signal/.plan/SG1.md).

## Validation

- `cmake --build build --config Debug --target SignalUnitTest` — passed.
- `ctest --test-dir build -C Debug -L signal --output-on-failure` — 13/13
  passed, including constant/impulse/sine fixtures, seed-zero golden values,
  noise partition/reset replay, empty requests and failed oversized request
  state preservation.
- `cmake --build build --config Debug --target KimPeanutEngine` — passed,
  including the Signal Lab consumer of the renamed generic generator API.
- Runtime GUI was not relaunched for this numerical-only change; the existing
  Signal Lab sine display was shown on the Default desktop in the prior slice.
- The headless target continues to link only the C++ standard library and
  `SampleBlock`; no window/audio/render/GPU services are initialized by tests.
