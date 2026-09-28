# Cornell Beauty recovery — 2026-09-28

## History and cause

User requested tracing historical Cornell settings and recovering the colored
Beauty appearance, then explicitly requested point-light intensity 10.
Task baseline: `11fea18`, with an existing uncommitted per-material diagnostic
log in `deferred_renderer.cpp`; that log was preserved.

`git log -- asset/level/cornell_box.level` identifies R4.6 commit `0e05180` as
its latest change before this task. The current level was identical to that
commit: camera (0,2.65,5.5), yaw -90, FOV 52, authored material overrides,
point light (-0.23,5,-3.04), intensity 450. Earlier R4.5 (`2341c88`) used FOV
65. The accepted historical image inspected was
`save/screenshots/validation/r46-beauty-final-fixed-seed-2026-09-25.png`.
Cornell material files exist locally but are not tracked by Git, so no material
commit history is asserted. Their scalar values remain red (0.445,0,0), green
(0,0.32,0), neutral (0.8,0.659341,0.43956), and emitter white with emission 2.

The level settings had not been lost. The recent PT packing rule changed every
base color to white whenever a bindless base-color texture slot existed.
StandardPbr also supplies a white fallback map for scalar-only materials, so
Cornell's valid red/green/neutral values were overwritten. The resolver already
selects identity tint for authored textures and retains authored scalar color
with the white fallback. Removing the redundant override restores that shared
policy without a Cornell-specific shader or material-table branch.

## Changes

- `engine/runtime/render/deferred_renderer.cpp`: removed the extra white-tint
  override; preserved the existing diagnostic logging and other source changes.
- `asset/level/cornell_box.level`: changed only ceiling point intensity 450 ->
  10, per the user's final instruction. Parsed JSON matches `0e05180` exactly
  after substituting that one value.
- This journal and `docs/status.md` record the correction.

Render continues to own material interpretation. No Graphics ownership,
shader ABI, dependencies or resource lifetime policy changed.

## Validation

- Debug engine and RenderSystemTest builds passed using `tools/kp.ps1`.
- `tools/kp.ps1 -Configuration Debug test -l render`: all 165 tests passed.
- Debug Vulkan launched outside the sandbox on the verified Default desktop.
  Runtime primary-albedo capture and material logs restore the red/green/neutral
  values. Accumulated Beauty at the historical intensity was inspected at
  2,956 pre-capture samples; colors, lighting and box shadows are visible.
- Reload after intensity 10 succeeded. Runtime reports active PT, four SPP,
  complete tracked residency, eight geometries and one instance. The final
  Beauty capture had 1,212 pre-capture samples. These are correctness captures,
  not performance windows or exact-history image comparators.
- Captures:
  `save/screenshots/validation/cornell-restored-beauty-20260928.png` and
  `save/screenshots/validation/cornell-restored-beauty-intensity10-20260928.png`.
- Level JSON parsing/history comparison and `git diff --check` passed.

Current Beauty is noisier than the long historical accumulation, and the
integrator has evolved since R4.6; pixel equivalence is not claimed. Sponza
was not relaunched after the final user instruction to only adjust intensity;
its authored-texture compatibility follows the shared resolver rule but a
fresh runtime regression is unverified. OpenGL and a repeat full suite were
not run for this small correction. No performance or R5 acceptance claim.


Final saved-setting check: a fresh Debug Vulkan launch at intensity 10 exported
`save/screenshots/validation/cornell-beauty-intensity10-fresh-20260928-1.png`
and active-PT stats in `save/diagnostics/cornell-recovery/intensity10-fresh-stats.json`.
The prior reload capture looked brighter than the preceding run, so it is not
used as a controlled intensity comparison. One capture attempt overlapped
shutdown and lost its connection; the subsequent fresh run captured normally.
The engine was closed through GLFW after verification.


## Historical soft-shadow difference — 2026-09-28

User identified that recovered colors do not restore the historical penumbra.
Source-history check confirms R4.6 (`0e05180`) sampled a rectangular emitter
at (-0.234011,5.3189155,-3.042968), half axes (0.65,0,0) and (0,0,0.525),
radiance (70,70,70). The rectangle is 1.3x1.05 world units. Commit `07ce893`
replaced that fixture-specific direct-light estimator with general scene
lighting. Current `DirectLight` samples each point source at its center; the
visible emissive ceiling mesh is not explicitly area-sampled. Its incidental
BSDF-hit contribution does not restore the old area-light estimator. Intensity
10 changes energy, not emitter size. Thus the preceding recovery restored
material colors, not the complete historical lighting model. Soft-shadow
recovery remains open and needs a general finite-area-light/emissive-surface
sampling contract; a Cornell-name branch or image blur would not restore it.
No source or light settings changed during this shadow diagnosis.
