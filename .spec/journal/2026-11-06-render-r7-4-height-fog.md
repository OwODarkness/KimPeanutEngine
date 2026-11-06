# R7.4 — Exponential height fog

- Status: accepted; R7.5 combined storage gate remains open.
- Date: 2026-11-06.
- Spec: [R7.4](../specs/render-r7-4-height-fog.md).
- Review: [R7.4](../../docs/render/.review/R7.4.md).
- Starting revision: `f1f2734` plus preserved uncommitted renderer ownership
  and stable-startup work. This follow-up is uncommitted.

## Implementation and placement

HeightFogPass owns its pipeline and GPU constants. It borrows the fullscreen
mesh/sampler and reuses the fullscreen pipeline description with TAA. It reads
the existing scene HDR and GBuffer depth. The graph leases a separate RGBA16F
output using the existing HDR description and routes it into TAA or tone map.
The same alpha channel carries transmittance for the existing capture conversion
pass. No additional diagnostic target, Asset dependency, RHI API or native
backend ownership was added. Graph variant encoding is colocated with the
condition schema; the facade selects inputs and schedules concrete owners.

Fog integrates along +Y exponential density with a denser-end anchor, a small
argument series and clamped log optical depth. Perspective starts at the eye;
orthographic starts at the pixel's origin on the camera plane. Sky stays
unchanged unless requested, in which case its ray is capped. Fog defaults off.
Zero density prunes the pass and avoids a history invalidation when it leaves
the same visible settings. Runtime copied settings live in the existing
RenderSystem mailbox; the command adapter has its own provider.

The spec records the local gkNextEngine reference and placement decisions.
The reuse harness searched camera/fullscreen, graph and capture symbols before
editing and passed its final structural checks. Graph binding and dispatch
remain local to DeferredRenderer because they compose the graph's owners.

## Validation

- `cmake --build build --config Debug --target KimPeanutEngine RenderSystemTest RenderPassScheduleTest RenderGraphTest` passed.
- `ctest --test-dir build -C Debug -L 'render|graphics' --output-on-failure`
  passed 260/260; the two new contracts cover graph routing/PT exclusion and
  visible settings/zero-density history behavior. Log: `save/logs/r74-focused-tests.log`.
- `glslc -DKP_GRAPHICS_API_VULKAN=1` and `glslangValidator -S frag` accepted
  the fog and capture shader sources for the tested backends. The runtime
  also compiled/loaded their actual built-in variants.
- `tools/validation/r7/height_fog_validation.py --port 37373 --prefix <prefix> --fixture <kind>`
  passed on visible Debug Vulkan/OpenGL Sponza, indoor `r7_raster_fixture`,
  and procedural `terrain-viewer`. The script uses `capture.screenshot` plus
  Runtime polling. The windows were verified as `GLFW30` on Default desktop.
  Scene color, transmittance and albedo captures are under
  `save/screenshots/validation/r74-20261106-*.png` and indexed by
  `save/diagnostics/r74-20261106-*.json`. Zero density was byte-identical for
  each fixture/API, and toggling while TAA is active reset and recovered history.
  The five analytic sky cases had maximum error 0.003; debug Vulkan showed no
  VUIDs. The captured Sponza, indoor and terrain images were visually inspected.
- The first launcher incorrectly waited for buffered stdout. Runtime socket
  readiness corrected this. Baked `level/terrain_baked.level` genuinely fails
  to load `model/terrain/generated/default` because it is absent from the
  archive. `terrain-viewer` passed with procedural geometry on both backends;
  no bake or archive modification was needed.
- `cmake --build build --config RelWithDebInfo --target KimPeanutEngine`
  passed (existing MSVC LNK4098 warning). The validator's `--measure` path ran
  three uncontended Vulkan windows on RTX 4070 Laptop GPU using native Sponza
  camera framing at 1094×631, complete residency, shadow maps, PT/query shadows
  off, AO and TAA effective, fog density 0.06. Validation was on in Debug
  correctness runs; the matching RelWithDebInfo exe supplied timing. Each
  window measured 120 warm-up and 300 samples. Fog p50/p95 (ms):
  0.021504/0.023552, 0.022528/0.023552, 0.022528/0.023552. Total GPU p95:
  3.3663, 3.3788, 3.3587 ms. Active payload/allocated bytes: 37,276,956 /
  38,051,840. Evidence: `save/diagnostics/r74-20261106-vulkan-sponza-rel.json`.

The 32 MiB combined storage ceiling is exceeded by 4.29 MiB at the measured
extent and remains an R7.5 gate. This change does not close R7.1 SSAO artifact
acceptance or the wider R5/R6 milestones. No dedicated orthographic fog capture
or broad test-suite rerun was performed for R7.4.

## Terrain Generation commit check — 2026-11-06

Ran the Debug executable with `--mode terrain-viewer --graphics-api <api>
--agent-port 37373` on the Default desktop, then ran
`height_fog_validation.py --port 37373 --fixture terrain --regenerate --prefix r74-terrain-regen-<api>`.
The Runtime command generated the 256×256 preview with seed 7426, lattice size
8, five octaves, persistence 0.5 and lacunarity 2. Both Vulkan and OpenGL published
revision 1 from revision 0. All six replay checks passed on both APIs, including
zero-density identity and TAA recovery. Vulkan logged no VUIDs and retired all
tracked RT resources on exit. No terrain bake was run. Results and captures are
indexed by `save/diagnostics/r74-terrain-regen-{vulkan,opengl}.json`.
