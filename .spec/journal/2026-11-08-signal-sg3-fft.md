# Signal SG3 radix-2 FFT

## Operation and acceptance

Implement forward and inverse radix-2 transforms in the headless Runtime
`Signal` target. Preserve the SG3 convention: forward negative exponent,
inverse positive exponent and `1/N` inverse scaling. Compare every seeded
complex output bin against the bounded direct DFT for N=8, 16, 32, 64, 128 and
256, with normalized L2 <= 1e-10 and maximum error <=
1e-10*max(1, N*max_abs_input).

## Ownership and reuse

The existing DFT reference and Signal CMake target are the direct algorithm and
module owner. Keep FFT in that Runtime domain and use immutable input spans
with an owned output vector. No external FFT dependency, GPU path, UI path or
new lifetime owner is introduced.

## Implementation and validation

- Added `ForwardFft` and `InverseFft` with in-place radix-2 butterflies over an
  output copy and bit-reversal permutation. Inverse applies `1/N`; sizes must
  be powers of two from 1 through 1,048,576.
- Validation rejects empty, oversized, non-power-of-two and non-finite inputs,
  output allocation failure, and non-finite transform results.
- Seeded differential tests compare each forward bin to `ForwardDft` at every
  planned size using the declared normalized L2 and maximum error budgets.
  Independent tests verify inverse sign and normalization and invalid inputs.
- `cmake --build build --config Debug --target SignalUnitTest` passed; it
  regenerated CMake and compiled the Runtime `Signal` library and test consumer.
- `ctest --test-dir build -C Debug -L signal --output-on-failure` passed 39/39.
- Placement evidence and `git diff --check` passed.

Windowed amplitude/PSD, aliasing and leakage experiments, and Spectrum UI remain
open; this slice verifies the transform core only.
