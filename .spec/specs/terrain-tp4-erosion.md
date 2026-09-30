# Terrain TP4 erosion postprocess

## Objective

Continue Terrain PCG from TP3 by placing height remapping and erosion after base
heightfield preparation in the recipe graph. Establish solver ownership and
contracts before implementing hydraulic or fluvial transport.

## Scope

- Move height remapping from the Terrain PBR profile into a generation operator.
- Support a monotone normalized-height curve that can flatten authored bands.
- Add deterministic talus relaxation as an immutable heightfield postprocess.
- Add implicit slope-exponent-one stream-power incision with D8 rerouting at
  each morphology iteration and cumulative-incision diagnostics.
- Chain the default 256² fixture through base noise, remap, talus and incision
  before preview mesh/material preparation.
- Document the remaining hydraulic, fluvial, conservation and comparison work.

Out of scope for this increment: sediment/water state, hydraulic transport, GPU
execution, chunking, and TP4 numerical acceptance.

## Invariants

- PCG nodes consume and return CPU values and do not depend on Runtime,
  Gameplay, Asset, Render, Graphics or Editor.
- Remap is a monotone function over normalized input height; it preserves the
  field endpoints and does not change grid coordinates.
- Talus returns a new field and transfers equal-and-opposite height volume
  between cells. It does not modify the input or claim sediment dynamics.
- Erosion is a postprocess and is independent of the base noise generator.
- Material bands blend locally around their own thresholds. Terrain geometry
  remapping does not silently alter a material's blend width.
- Bake provenance retains the full ordered recipe and operator versions.

## Stages

1. Define normalized curve semantics and remove height remap from material
   configuration.
2. Implement a deterministic versioned talus postprocess and add it to the
   checked-in preview recipe.
3. Build the engine, inspect the Terrain Viewer output, and record current
   evidence and numerical gaps.
4. Follow up with numerical fixtures and bounded solver comparisons before
   marking any erosion algorithm accepted.

## Acceptance

- [ ] Remap example maps the normalized band 0.2–0.5 to 0.2–0.25 with finite,
  monotone values and preserved endpoints.
- [ ] Talus flat/sub-repose inputs are unchanged; above-repose slopes relax
  deterministically while conserving height-volume within tolerance.
- [ ] Viewer preview consumes the postprocessed field and material transitions
  remain local to profile thresholds.
- [ ] Talus and incision numerical contracts pass, including volume
  conservation, bounded erosion, outlet behavior and reproducibility.
- [ ] Hydraulic transport separately establishes water/sediment state,
  boundaries, timestep, source/sink budgets and numerical tolerances.
- [ ] Matched resolution/memory/bake-time experiment records solver selection.

This increment implements the graph placement and prototypes only. It does not
claim TP4 acceptance until its numerical checks pass.

## Validation

- Build the Debug `KimPeanutEngine` target.
- Run targeted Terrain contracts and the finite/reproducibility/remap/volume
  fixtures after the implementation policy permits test execution.
- Inspect a Debug Vulkan Terrain Viewer capture and confirm final mesh creation
  uses `stream_power_incision.height`; run OpenGL preview after cross-backend
  build.
- Use `git diff --check` and inspect staged ownership/API changes.

## Risks

- Global min/max remapping is not tile-invariant; chunked generation must use a
  shared authored range.
- The current talus prototype lacks numerical test evidence and may need its
  flux law or convergence controls revised.
- Hydraulic transport cannot be represented by a height-only field. It needs
  typed water, sediment and diagnostic channels before implementation.
