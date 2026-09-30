# TP4 — erosion as generation postprocesses

## Goal

Compose a seeded base heightfield first, then apply independently versioned
erosion operators to its immutable output. Erosion belongs in the PCG graph as
postprocessing, before final slope/material analysis and mesh conversion. A
recipe may omit erosion or reorder compatible postprocesses without coupling a
noise generator to a solver.

```text
base terrain -> normalized height remap -> erosion postprocess(es)
             -> final drainage/slope/curvature -> material fields -> mesh
```

Height remapping is an authorable monotone curve over the input field's
normalized min/max. Control points such as `(0.2, 0.2)` and `(0.5, 0.25)` flatten
that elevation band while preserving its relative ordering. PCHIP interpolation
keeps the curve monotone without the abrupt derivative resets of per-segment
smoothstep. Material thresholds remain independent and only blend inside their
local configured widths.

## Boundaries and invariants

- TerrainCore/TerrainGeneration operate on CPU value fields only. They do not
  depend on Perlin, Runtime, Gameplay, Asset, Render, Graphics, or Editor APIs.
- Erosion nodes consume a heightfield and return new immutable products; they
  never mutate or alias the input. Mesh conversion and final derived fields
  consume the postprocessed result.
- Operator IDs and versions pin algorithm semantics. Node IDs, parameters,
  source hashes, seed and domain stay in bake provenance.
- Global drainage/erosion evaluates the entire finite domain before any TP7
  chunk split. Boundaries and outlet policy are explicit.
- Numerical budgets use meters, square meters, cubic meters and seconds where
  applicable. Any exported water/sediment balance includes declared sources,
  sinks and open-boundary flux.
- Cancellation is observed at deterministic iteration boundaries. A cancelled
  evaluation publishes no partial field; the viewer keeps its previous bake.

## Solver selection

1. **Talus relaxation prototype:** deterministic closed-domain conservative
   transport across neighboring cells above a configured repose slope. It is a
   thermal/gravity postprocess and must conserve elevation-volume to tolerance.
2. **Fluvial incision prototype:** drainage plus implicit stream-power incision
   with routing recomputed as morphology changes. Track bedrock lowering and
   exported sediment separately; do not claim sediment conservation if transport
   and deposition are absent.
3. **Grid hydraulic baseline:** a CPU virtual-pipe solver carries bedrock,
   soil, sand, water and suspended sediment as separate channels. Rain,
   evaporation and open-boundary export are explicit; each timestep gathers
   from distinct water/sediment buffers and publishes water/solid budgets.
   This is the first integrated hydrology model, not a state-of-the-art claim.
4. Compare the baseline to thermal relaxation on the same layered
   `island_macro_256` input (256²). Keep
   the 2026 stochastic geomorphological transport model as the next research
   comparison; adoption requires a reproduced solver, quality tiers, memory
   and bake-time measurements.

An optional CPU thermal-flux postprocess is available after the macro
landform. For each iteration it computes outgoing material flux for all eight
neighbors, then gathers flux into a distinct height buffer. Recipes that use it
can expose the angle of repose, transport rate and iteration count, and compare
input/output heightmaps. The default preview recipe currently bypasses
erosion, so base landform appearance can be evaluated independently. This
preserves the CPU-only PCG/RHI boundary. The operator is still a preview
prototype, not numerical acceptance or GPU execution. The original talus
relaxation and stream-power operators remain available as separate graph nodes.

The initial virtual-pipe implementation is registered as
`terrain.erosion.hydraulic_pipe@1`. It consumes a typed immutable layered
heightfield and rain/erodibility/hardness/obstacle fields, then emits updated
material channels, flow/discharge, export and budget diagnostics. The matched
256² Debug comparison confirms water movement, nonzero changes from both
methods, and water/solid relative residuals below `1.3e-10`. No water reached
the open boundary during this one-second run. This closes the first operator
integration and comparison, but does not accept timestep convergence,
resolution scaling, artist-facing controls, or visual quality.

A GPU implementation remains deferred until the common RHI has a compute
pipeline/dispatch contract; TP9 must compare it to this CPU reference without
introducing backend dependencies into TerrainGeneration.

## Validation and exit criteria

- Remap fixtures include constant, plane and monotone multi-control-point data;
  the sample interval 0.2–0.5 maps to 0.2–0.25, endpoints stay fixed, and the
  output remains finite and monotone.
- Talus fixtures include flat, sub-repose plane, above-repose plane and basin;
  no-op cases remain byte-identical, transport is deterministic, and total
  height-volume is conserved within a resolution-derived tolerance.
- Hydraulic matched fixture proves nonnegative typed state, explicit rain,
  bounded surface change, finite state, reproducible evaluation and water/solid
  residuals below `1.3e-10`. Timestep convergence,
  more boundary fixtures and stream-power acceptance remain open.
- Compare morphology and cost at 128², 256² and a bounded higher resolution;
  record solver state, units, iterations, peak CPU memory and bake time.
- Viewer/runtime captures demonstrate that postprocessing runs after base
  generation and before mesh/material preparation on Vulkan and OpenGL.

No acceptance is inferred from screenshots or compilation alone. The GPU path
is a later TP9 investigation after a deterministic CPU reference is accepted.

## Research basis

- Pangolin's `TerrainActor::CreateVerticles` evaluates `HeightRemap` per height
  sample before creating mesh vertices. That supports a geometry/PCG remap, not
  a material-transition remap. No reusable original layer-blend implementation
  was identified in the inspected Pangolin code.
- Argudo et al., *Terrain descriptors for landscape synthesis, analysis and
  simulation* (Computer Graphics Forum, 2025), describes hydraulic erosion as
  sediment transport/deposition, thermal processes separately, and contrasts
  D8 and multiple-flow routing for terrain synthesis:
  <https://onlinelibrary.wiley.com/doi/10.1111/cgf.70080>.
- Braun and Willett, *A very efficient O(n), implicit and parallel method to
  solve the stream power equation governing fluvial incision and landscape
  evolution* (Geomorphology, 2013), motivates an implicit fluvial baseline:
  <https://doi.org/10.1016/j.geomorph.2012.10.008>.
- Jako and Szirmay-Kalos, *Fast Hydraulic and Thermal Erosion on GPU* (2011),
  and Mei et al., *Fast Hydraulic Erosion Simulation and Visualization on GPU*
  (2007), are graphics-oriented references. Their GPU implementations do not
  set this CPU evaluator's ownership or synchronization model.
- The integrated CPU baseline uses the classic virtual-pipe family described
  by Mei et al. It is chosen for explicit local water/sediment state and a
  bounded, testable CPU reference, not because it is the newest method.
- Argudo et al.'s 2026 *Stochastic geomorphological transport for terrain
  erosion simulation* is the state-of-the-art candidate for a later matched
  study. The public `geotransport` repository contains its generalized
  transport reference; its README points to `soillib` for the complete erosion
  model. That full implementation is CUDA/C++23 and was not ported or
  independently reproduced in this C++20 CPU milestone.
- The 2026 stochastic-transport experiment is a comparison candidate only;
  adoption requires reproducible source, budgets and matched measurements.

## Remaining risks

- Height remap normalization is domain-global. For chunked or tiled terrain,
  preserve a shared authored range; per-tile auto-normalization would create
  seams and change material thresholds.
- Hydraulic timestep convergence, nonuniform/rainfall scenarios, resolution
  sensitivity, and UI selection/visual comparison remain open. A successful
  mass ledger alone does not validate realistic channel morphology.
- Talus transport can converge slowly and may blur fine detail; quality settings
  require measured resolution scaling and artist review.
