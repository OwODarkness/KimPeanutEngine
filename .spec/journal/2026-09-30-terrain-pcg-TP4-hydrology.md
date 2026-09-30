# TP4 initial hydrology integration — 2026-09-30

## Decision

Integrated a CPU virtual-pipe hydraulic erosion operator as the first
hydrology reference in TerrainGeneration. It is not described as state of the
art. The 2026 stochastic geomorphological transport method remains the SOTA
comparison candidate: the inspected public `geotransport` repository is a
generalized transport reference and points to `soillib` for the full model;
the inspected full library is CUDA/C++23 and would need a separately scoped
port and validation before it can be compared fairly with this C++20 CPU path.

## Implementation

- Added immutable `LayeredHeightfield2D` state with bedrock, soil, sand, water,
  suspended sediment, and derived surface elevation channels.
- Registered `terrain.state.layered_heightfield@1` and
  `terrain.erosion.hydraulic_pipe@1` in the PCG operator registry.
- The hydraulic operator applies rain, virtual-pipe water flux, sediment
  advection, capacity-based soil erosion/deposition, optional bedrock
  weathering, evaporation, and closed/open boundary policies.
- Added cancellation checks at hydraulic substep boundaries and reports for
  flow/discharge, exported water/sediment, and relative water/solid budgets.
- Kept the operator in TerrainCore/TerrainGeneration with no Gameplay, Render,
  Graphics or backend ownership.

## Matched comparison

Debug configuration, checked-in `island_macro_256` 256×256 terrain with
0.3 m initial soil, thermal 20 iterations, hydraulic duration 1 s with maximum
timestep 0.05 s. Both branches start from the same layered surface. This is a
correctness/morphology diagnostic, not performance acceptance.

| Measurement | Thermal flux | Hydraulic virtual-pipe |
|---|---:|---:|
| Node evaluation time | 487.314 ms | 897.157 ms |
| Height RMSE from input | 0.384174 m | 0.029116 m |
| Peak discharge | n/a | 2.33598 m³/s |
| Exported water | n/a | 0 m³ |
| Relative water budget residual | n/a | 1.20×10⁻¹⁰ |
| Relative solid budget residual | n/a | 1.21×10⁻¹⁰ |

The test also verifies a layered state is returned and both solvers produce a
nonzero terrain change. In this one-second fixture run, the hydraulic solver
retains rainwater within the island and does not export it through the open
boundary. The metrics establish local water movement and conservation, but not
convincing channels, timestep convergence, resolution scaling, or controls.

## Validation

- `cmake --build build --config Debug --target TerrainGeneration` — passed.
- `cmake --build build --config Debug --target TerrainGenerationTest` — passed.
- `build/engine/test/unit/terrain/Debug/TerrainGenerationTest.exe --gtest_filter=TerrainHydrologyTest.*` — passed; comparison metrics above.
- `cmake --build build --config Debug --target TerrainViewer` — passed after adapting editor/host scalar views to the evaluator's typed values.
- Full `TerrainGenerationTest.exe` — 15/16 passed. The existing mesh test expects 24 indices while the current mesh builder returns 96; this assertion is outside the hydrology change. CTest discovered no tests for this target in the configured build tree.

## Follow-up

- Add basin/channel, zero-rain, closed-boundary and timestep-convergence
  fixtures; measure 128²/256² and memory under a release configuration.
- Add viewer method selection and inspect rendered output before promoting the
  hydraulic pass to the default terrain recipe.
- Reproduce the complete 2026 stochastic solver or document a bounded port
  plan before claiming a direct quality comparison.
