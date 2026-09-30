# Terrain PCG TP4 erosion prototype

- Status: in progress
- Date: 2026-09-30
- Stage design: [TP4](../../docs/terrain/.plan/TP4.md)
- Implementation contract: [Terrain TP4 erosion](../specs/terrain-tp4-erosion.md)
- Parent roadmap: [TP4](../../docs/terrain/TODO.md#tp4--rain-erosion-fluvial-incision-and-solver-selection)

## What changed

- Moved height remapping out of the PBR profile and into
  `terrain.heightfield.remap`. The normalized PCHIP curve is monotone and the
  default fixture compresses the authored 0.2–0.5 interval to 0.2–0.25 while
  retaining 0 and 1 endpoints.
- Added the deterministic `terrain.erosion.talus_relax` heightfield
  postprocess. Each iteration computes equal-and-opposite neighbor transfers
  for slopes above the repose angle and observes cancellation at iteration
  boundaries.
- Added `terrain.erosion.stream_power_incision`. Each iteration routes D8
  drainage from the current surface, applies a bounded implicit update for
  slope exponent one, and exports both the updated height and cumulative local
  incision field.
- Updated the 256² preview chain to run base landform/detail, height remap,
  talus relaxation and stream-power incision before final material/mesh
  preparation. Material thresholds remain localized to their configured blend
  widths; slope overlays remain limited to their elevation range.
- Added the TP4 stage plan and execution spec. Hydraulic water/sediment state,
  numerical acceptance, resolution comparison and the 2026 method experiment
  remain open.

## Ownership and behavior

TerrainCore/TerrainGeneration still own CPU values and algorithms only. The
erosion operators do not depend on Perlin implementation details or on Runtime,
Gameplay, Asset, Render, Graphics or Editor. `TerrainViewerHost` consumes the
final `stream_power_incision.height` output using the existing Gameplay Actor,
MeshComponent and RenderSystem route.

The two erosion nodes are implementation prototypes, not validated solver
acceptance. Talus volume error has not been measured. Stream-power parameters
use a normalized drainage-cell fraction and per-iteration meter caps; they have
not yet been calibrated against a geomorphic reference. No hydraulic or
suspended-sediment simulation is claimed.

## Validation

- `tools/kp.ps1 build KimPeanutEngine` — PASS, Debug, after closing the running
  viewer so the linker could replace the executable.
- Runtime: Debug Vulkan `--mode terrain-viewer --graphics-api vulkan
  --agent-port 37373`; Runtime `capture.screenshot` exported
  `save/screenshots/validation/terrain-tp4-vulkan-final.png`. The window was
  verified as `GLFW30`, foreground on the `Default` desktop. The screenshot
  shows the 256² terrain and selects `stream_power_incision.height`.
- Runtime: Debug OpenGL `--mode terrain-viewer --graphics-api opengl
  --agent-port 37373`; Runtime `capture.screenshot` exported
  `save/screenshots/validation/terrain-tp4-opengl-final.png`. The scene and
  docked viewer panels rendered; Runtime displayed OpenGL and the final
  postprocessed heightfield selection.
- No tests were added or run in this increment. No FPS acceptance measurement
  was taken; screenshots were visual checks, not performance evidence.

## Remaining TP4 work

- Add and run remap, talus flat/plane/basin, volume, bounded-incision,
  reproducibility and outlet fixtures with stated tolerances.
- Add conservative hydraulic water and sediment transport with explicit rain,
  evaporation, boundary flux and timestep accounting.
- Run the bounded comparison at matched resolutions, record morphology, memory
  and bake time, and decide whether to adopt the 2026 stochastic method.
- Revisit material thresholds if the author wants smaller blue lowland coverage;
  the current blue band is still only a height mask, not a routed river.
