# Cornell rectangular area light — 2026-09-28

## Scope and acceptance

User requested an area light after historical Beauty comparison exposed hard
point-light shadows. Baseline HEAD: `11fea18`; working tree already contained
Cornell intensity 10, scalar-material packing recovery, diagnostic material
logging, and their documentation. Preserve these changes.

Boundary: scene-authored rectangle through Asset → Level → Gameplay source →
Render snapshot → PT light table. No Cornell-specific shader branch and no
Graphics allocation/lifetime changes. Accept when Debug Vulkan Beauty and
direct-only captures show finite-source penumbrae, color recovery remains,
and affected loader/lifecycle contracts pass. No performance claim.

## Design and implementation

`area_light` reuses the local-light lifecycle with two half axes appended to
existing value records. Default zero axes preserve existing point-light
fixtures and aggregate initializers. Parser and Render reject degenerate or
non-perpendicular rectangles. Gameplay rotates the axes with the world
transform; size is explicitly authored rather than inherited from scale.

PT packs type 3, center, half axes, radiance, range, and shadow intent into the
existing four-vec4 record. It remains 64 bytes; the scene uniform table is not
enlarged. Direct lighting samples the rectangle per path sample/bounce and
uses the uniform area PDF converted to solid angle. Primary direct lighting
is resampled within the sample loop for finite emitters so multiple samples
per dispatch use different light points. Punctual-only scenes retain their
single cached primary direct-light evaluation. Both visibility implementations
use the same sample.

Cornell uses a 1.3 × 1.05 rectangle, centered at
`(-0.234011, 5.315, -3.042968)`, slightly beneath the historical emitter plane
to avoid occlusion by its ceiling geometry. Cross(+X,+Z) faces down. Intensity
remains the user-requested 10, now interpreted as emitted radiance. Exposure,
camera, material assignments, and bounce policy are not edited by this task.

Raster retains center-point lighting/shadow approximation. The analytic
rectangle is independent of mesh emission and has no ray-intersectable shape;
existing weak mesh emission is retained. This is not a general emissive-mesh
sampler or a mesh-light MIS implementation. See [usage](../../docs/render/usage.md).

## Reference study

Repository-specific source search was unavailable; used primary-source web
access to [pbrt-v4 master, src/pbrt/lights.cpp](https://github.com/mmp/pbrt-v4/blob/master/src/pbrt/lights.cpp),
`DiffuseAreaLight::SampleLi` and its invisible-area-light constructor case.
The reference samples a surface point and returns emitted radiance, direction,
and PDF; its invisible emitters use light sampling alone. Applied the sample
and PDF principle to our analytic rectangle without importing source.
Unlike pbrt's normal visible area emitters, our new rectangle is not associated
with a geometric primitive. Commit revision was not pinned; branch is master.

## Validation

Validation level 4 because the shape crosses shared value records.

- `tools/kp.ps1 -Configuration Debug build KimPeanutEngine`: passed.
- Initial raw full `cmake --build build --config Debug` failed before useful
  compilation with MSBuild's duplicate `PATH` / `Path` environment exception.
  Retried using `tools/kp.ps1 -Configuration Debug build`: full build passed.
- `glslangValidator -V --target-env vulkan1.2 asset/shader/ray_tracing_path_tracer.rgen
  -o save/diagnostics/cornell-area-light.rgen.spv`: passed after final shader edits.
- Full `ctest --test-dir build -C Debug --output-on-failure`: all 999 tests
  passed (166 render tests), 237.87 seconds. Includes the three new rectangle
  parsing, lifecycle/shape-validation, and component-rotation contracts.
- `git diff --check`: passed with line-ending notices.

Runtime launches used approved execution outside the sandbox. The launcher
verified desktop `Default`, used the rebuilt Debug Vulkan executable and
`level/cornell_box.level`, and left normal Debug validation enabled. Captures
were requested through Runtime `capture.screenshot` and polled to completion.
Camera: `(0,2.65,5.5)`, authored 52-degree FOV; viewport 1094 × 631. RT active,
8 geometry records, 1 instance, 8 materials, 1 light; texture residency complete.
Environment disabled, 8 continuation bounces, 4 samples per dispatch,
reconstruction off. Recorded samples are immediately before capture, not an
exact export-sample count. These are correctness captures; concurrent build
work and Debug timings do not constitute performance evidence.

| Capture | Mode / visibility | Samples before capture |
| --- | --- | ---: |
| `cornell-area-beauty-converged-20260928.png` | Beauty / trace ray | 2072 |
| `cornell-area-direct-converged-20260928.png` | Direct Only / trace ray | 9184 |
| `cornell-area-small-control-20260928.png` | Direct Only / trace ray | 2188 |
| `cornell-area-ray-query-20260928.png` | Beauty / inline query | 2236 |

All captures are under `save/screenshots/validation/`. Beauty retains the
red/green walls and warm neutral boxes. Wide-emitter direct-only capture has
visible penumbrae on the floor and left wall. The temporary control fixture
used the same center/camera/materials with half axes `(0.065,0,0)` and
`(0,0,0.0525)` and intensity 1000: ten-times-smaller side lengths, one hundredth
the area, equal emitted power. Its shadows become much sharper. This isolates
finite source size from indirect lighting and verifies the requested effect.
The temporary control file was removed; saved Cornell remains wide/intensity 10.

Both Beauty visibility implementations visually agree. Display-RGB MAE over
scene crop x=208..834, y=14..630 is 0.00124 between the converged captures;
they have different sample counts, so this is a consistency check rather than
exact numerical parity. Initial exploratory direct capture had pre-switch
Beauty stats and is excluded from acceptance evidence; subsequent captures
waited for the effective output/visibility mode before exporting.

Logs inspected: `save/logs/2026-09-28/KimPeanutEngineLog-2026.09.28-12.31.54.txt`,
`...-12.33.25.txt`, and `...-12.34.01.txt`. No logged Vulkan validation error or
device loss. The final close logs existing zero-scene and transient-target-pool
warnings during teardown; final RT resource counts are zero. Windows were
closed via each process's GLFW handle; no engine process remains.

## Changed files and limits

- Fixture/shader: `asset/level/cornell_box.level`,
  `asset/shader/ray_tracing_path_tracer.rgen`.
- Asset/Level: `engine/runtime/asset/level.h`, `level_loader.cpp`,
  `engine/runtime/level/level_instance.cpp`.
- Gameplay: `component/point_light_component.{h,cpp}` and
  `factory/point_light_actor_factory.{h,cpp}` under `engine/runtime/gameplay/`.
- Render: `engine/runtime/render/light/light_source.h`, `light_world.{h,cpp}`,
  `light_source_registry.cpp`, and `engine/runtime/render/deferred_renderer.cpp`.
- Tests: `engine/test/unit/asset/level_loader_test.cpp`,
  `engine/test/unit/gameplay/gameplay_world_test.cpp`,
  `engine/test/unit/render/light_source_registry_test.cpp`.
- Documentation: Render usage/plans/roadmap, project status, and this journal.

No RHI/GPU layout, allocation owner, synchronization, or dependency direction
changes. Existing intensity 10 and scalar-color recovery are preserved;
preexisting material logging is retained. Remaining limits: raster soft area
shadows/LTC, visible analytic geometry, mesh-light linkage/MIS, and Editor
shape controls are not implemented. OpenGL fallback and Sponza runtime were
not exercised by this task. No RelWithDebInfo timing or speedup claim.
