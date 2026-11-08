# Signal SG3 scalar DFT reference

## Operation and acceptance

Implement the direct complex DFT and inverse in the headless Runtime `Signal`
target. Fix the convention to a negative-exponent forward transform and a
positive-exponent inverse with `1/N` scaling. Independently verify bin order,
sign, scaling, analytic spectra, and invalid input. Keep the quadratic reference
bounded so callers cannot mistake it for the later FFT path.

## Ownership and reuse

**Existing candidates inspected:** `engine/runtime/signal/signal_statistics.*`
establishes the headless numerical API style and diagnostic convention.
`engine/runtime/audio/editor/audio_player_controller.cpp` contains a small
display-oriented FFT; its scaling and presentation contract are local to Audio
Player and are not reused. The pre-existing deletion of
`engine/runtime/core/signal/dft.*` remains untouched.

**Placement and reason:** `engine/runtime/signal/dft.*` extends the existing
Runtime `Signal` domain, which owns scalar reference algorithms and the future
FFT. Signal Lab remains a consumer. No new target, dependency, state owner, or
GPU/UI coupling is introduced.

## Implementation and validation

- Added `ForwardDft` and `InverseDft` over complex double spans; both return an
  optional owned vector and diagnostic. The direct reference accepts 1–1,024
  values, rejects non-finite input and reports accumulation/allocation errors.
- Added independent impulse, DC, positive-exponent tone-bin, inverse-sign and
  inverse-normalization tests, plus a complex round trip and empty, non-finite,
  oversized, and accumulation-overflow rejection.
- `cmake --build build --config Debug --target SignalUnitTest` passed after
  CMake regenerated the project for `dft.cpp`, compiling the Runtime library
  and its headless test consumer.
- `ctest --test-dir build -C Debug -L signal --output-on-failure` passed 35/35.

This slice establishes the DFT oracle only. Radix-2 FFT comparison, windows,
amplitude/PSD conventions, aliasing experiments, and Spectrum UI remain open.
