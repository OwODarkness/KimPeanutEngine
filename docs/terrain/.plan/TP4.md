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
3. **Grid hydraulic prototype:** conservative water and suspended-sediment
   transport with explicit rain, evaporation and open-boundary budgets. Bound
   the stable timestep and keep all state buffers distinct per iteration.
4. Compare those baselines to the 2026 stochastic geomorphological transport
   method on the same domain and quality tiers. Record morphology, conservation
   error, resolution sensitivity, memory and bake time before adopting it.

The first implementation increment includes the isolated talus node, an
implicit slope-exponent-one stream-power incision prototype with routing
recomputed each iteration, and a visibly flattening PCG remap. It is not TP4
solver acceptance. Hydraulic transport, numerical contract fixtures, quality
comparison and the 2026 method adoption decision remain subsequent work.

## Validation and exit criteria

- Remap fixtures include constant, plane and monotone multi-control-point data;
  the sample interval 0.2–0.5 maps to 0.2–0.25, endpoints stay fixed, and the
  output remains finite and monotone.
- Talus fixtures include flat, sub-repose plane, above-repose plane and basin;
  no-op cases remain byte-identical, transport is deterministic, and total
  height-volume is conserved within a resolution-derived tolerance.
- Hydraulic fixtures prove nonnegative water/sediment, explicit source/sink
  accounting, bounded erosion, finite state, reproducibility and timestep
  convergence. Stream-power fixtures prove bounded incision and outlet policy.
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
- The 2026 stochastic-transport experiment is a comparison candidate only;
  adoption requires reproducible source, budgets and matched measurements.

## Remaining risks

- Height remap normalization is domain-global. For chunked or tiled terrain,
  preserve a shared authored range; per-tile auto-normalization would create
  seams and change material thresholds.
- A height-only product cannot represent water velocity or suspended sediment
  state. Hydraulic transport needs explicit typed channels before acceptance.
- Talus transport can converge slowly and may blur fine detail; quality settings
  require measured resolution scaling and artist review.
