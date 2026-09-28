# Sponza PT wall surface parity — 2026-09-28

## Request and baseline

Diagnose and correct the wall base-color difference between PT and raster,
after the previous scalar-texture rule and ray-cone LOD changes did not fix it.
HEAD: `9089df26325b5976b0ab2bc20bde46febd2ac9a2`, with existing R5.1, environment,
shader and documentation changes. Those changes were preserved. The task-start
versions of touched source/test files were saved under
`save/diagnostics/sponza-albedo/`; they distinguish this correction from earlier
uncommitted work. No shader, fixture, Graphics or vendored source was changed
by this task.

## Cause and correction

Raster `RecordGBufferPass` submits only the opaque draw list. Sponza's imported
`dirt_decal` material is alpha-blended: the source glTF has `alphaMode: BLEND`
and alpha factor 0.350000024. Decal geometry lies in front of the stone. RT
scene preparation previously included every visible section; Vulkan builds
its triangles opaque and ray shaders request opaque intersections. PT therefore
sampled the decal's base color instead of the underlying stone. A valid material
lookup at the wrong visible surface explains why changing tint or LOD could not
resolve the wall mismatch. The missing normal map cannot cause an albedo-probe
difference.

Render now selects opaque sections for the current opaque RT surface domain,
matching raster's existing unsupported-blending policy. Geometry filtering
retains original section indices for PT material packing. BLAS sharing uses
mesh plus the exact retained section list, so two instances with different
material draw-class overrides cannot share the wrong subset. World/material
revision invalidation and unchanged-frame record reuse remain in place.
Graphics still owns creation, synchronization and retirement; no RHI contract,
shader ABI or dependency direction changed. Cached BLAS variants retain the
existing renderer-owned cache lifetime and are released through Graphics at
RT teardown; per-variant eviction is not added here.

Changed source: `deferred_renderer.{h,cpp}`, new
`ray_tracing_scene_sections.h`, and three regression tests in
`render_system_test.cpp`. Tests cover mixed-section material remapping,
all-blended omission, and sharing/isolation for material overrides and mesh
handle generations.

## Controlled runtime evidence

Debug Vulkan, normal validation enabled, Sponza, 1094x631, camera position
(10.807430267334, 1.592219948769, 0), authored rotation (0,180,0), four SPP/eight
continuation bounces,
primary-albedo output, unchanged shader/fixture, IBL 0.35. Both accepted runs
report complete tracked texture residency. Baseline is a preserved executable
from the task-start Debug build; candidate has the section filtering correction.
Binary/source hashes and complete Runtime stats are retained in
`save/diagnostics/sponza-albedo/`.

The first captures were premature (26 textures incomplete) and are not the
comparator. Full residency restores curtain detail but leaves the baseline
wall mismatch. `MaterialSystem::RefreshResources(force_ready)` advances the
revision on texture promotion, so stale PT scene-cache invalidation is not the
source explanation supported here.

| Capture pair | RT geometry records | Instances | Original back-wall region linear RGB MAE | Clear stone patch linear RGB MAE |
| --- | ---: | ---: | ---: | ---: |
| Fully resident baseline | 450 | 3 | 0.13109 | 0.12823 |
| Fully resident corrected | 446 | 3 | 0.04003 | 0.00623 |

Original region: [490,130,605,200), including foliage/edges. The additional
clear stone rectangle [585,190,610,210) was selected after inspecting the image
to avoid cutout/edge contamination; it is supporting evidence, not a
whole-image acceptance score. Each PNG is decoded from sRGB; PT's Reinhard
mapping is inverted as `c/(1-c)` before comparison with the raster base-color
capture. This is an 8-bit display diagnostic, not raw HDR readback. Banner MAE
0.001406 and floor MAE 0.008758 are unchanged by the correction. The raster
comparison images are pixel-identical, isolating the RT surface-policy intervention.

Captures, under `save/screenshots/validation/`:

- `sponza-albedo-baseline-resident-{scene_color,base_color}.png`
- `sponza-albedo-corrected-resident-{scene_color,base_color}.png`
- `sponza-albedo-corrected-beauty.png`
- `sponza-albedo-corrected-ray_query_visibility.png`
- `sponza-albedo-corrected-reload-resize.png`

Inspected the baseline/corrected albedo and corrected Beauty images. Beauty now
shows the underlying stone; its short accumulation is visibly noisy and is not
an equal-sample final-lighting comparison. Both traceRay and query visibility
capture paths remain active. Reload recovers PT with 446 geometry records and
full residency; steady stats report zero scene-table records packed. Window
resize returned `applied: false` in its immediate queued response, but completed
Runtime stats and the subsequent capture verify viewport 560x389 instead of
1094x631 with PT still active. This covers one resize, not extended resize stress.

## Validation and limits

- `.\tools\kp.ps1 -Configuration Debug build KimPeanutEngine`: candidate passed.
- `.\tools\kp.ps1 -Configuration Debug build RenderSystemTest`: passed.
- `.\tools\kp.ps1 -Configuration Debug test -l render`: all 165 passed.
- Runtime launches were outside the sandbox, verified on the Default desktop.
  Captures used Runtime `capture.screenshot`/poll. Close used the process's
  GLFW30 window, not the NVIDIA helper. No foreground-focus claim was made.
- Baseline/candidate logs contain no VUID, validation-error/warning or
  VK_ERROR diagnostics in the inspected runs.
- Final header include cleanup, broader suite and final diff checks are recorded
  below when complete.

The dominant wall mismatch is corrected; complete pixel/material parity is
not claimed. Blended transport/decals are omitted by both current paths rather
than rendered transparently. Alpha-masked cutouts, PT normal maps,
ray-cone versus raster anisotropic filtering, OpenGL and Cornell regression,
and override stress/lifetime testing remain separate follow-up. No performance
measurement or speedup is claimed. Future R5 comparisons must distinguish the
446-section corrected scene from the old 450-section scene.


## Final validation follow-up

The final Debug engine and RenderSystemTest builds passed after replacing the
helper's transitive mesh include with its direct common handle include. A final
binary Cornell smoke produced active PT and albedo/base-color/Beauty captures,
then closed normally. Its Beauty image is heavily overexposed; without a
matched Cornell baseline this is not visual-regression acceptance and is not
attributed to this correction. Sponza provides the matched visual evidence.
OpenGL runtime remains unrun. `git diff --check` passed with line-ending notices;
new documentation link targets resolve. Full CTest result is recorded next.


`ctest --test-dir build -C Debug --output-on-failure` passed all 996 tests
(277.28 seconds), including all 165 render tests on the final test build.
No engine process remains running. Work remains uncommitted; generated binaries,
logs and captures are not staged.
