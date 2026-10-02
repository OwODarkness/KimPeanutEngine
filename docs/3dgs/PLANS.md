# 3D Gaussian Splatting Module Plan

**Status: design only (2026-09-30).** The code currently contains an empty
`GaussianPrimitive` in `engine/module/3dgs/3dgs_model.h`; there is no 3DGS
target, loader, renderer, or fixture. The [roadmap](TODO.md) tracks acceptance,
the [GS1 stage plan](.plan/GS1.md) gives the first implementation slice, and
the [reference study](references.md) records the evidence behind the choices.

## Objective and boundary

Render trained, static 3D Gaussians in the engine's scene view, initially from
the original GraphDeco PLY format. The first release covers import, camera
movement, ordered anisotropic splats, mesh/splat compositing in raster mode,
capture, resize, and teardown. It does not train, differentiate, animate, or
make Gaussians participate in mesh lighting, shadows, collision, or path tracing.

The module is optional and owns its Gaussian-specific CPU schema and draw
preparation. The general engine layers keep their existing responsibilities:

```text
GraphDeco PLY -> Asset importer/validated Gaussian model -> Resource packed artifact
             -> module scene instance and frame draw packet
             -> Render graph: sort/upload -> splat accumulation -> HDR composite
             -> Graphics/RHI buffers, targets, commands -> Vulkan / OpenGL
```

Asset owns the identity, source dependency, reload transaction, and immutable
CPU payload. A scene instance owns transform and visibility; it references the
asset without changing it. Render owns the active camera, pass dependencies,
target choice, and composition with meshes. Graphics owns allocations, uploads,
barriers, and safe retirement. The module never loads a file during pass
recording. The empty `GaussianPrimitive` is a placeholder, not an API to fill
before the CPU schema is chosen.

## Format and model decision

Use a narrowly validated GraphDeco PLY importer first. Decode position,
`f_dc_*`, ordered `f_rest_*` coefficients through SH degree 3, opacity logit,
log scale, and quaternion from the source. Convert activation once at import:
`sigmoid(opacity)`, `exp(scale)`, normalized rotation. Store canonical activated
values, SH coefficient order, and the source coordinate convention in a
versioned native CPU product;
test the conversion against known samples. PLY's `nx/ny/nz` placeholders are
not surface normals for lighting. Reject malformed counts, missing fields,
non-finite values, invalid scales, and oversized allocations with diagnostics.

The native model should separate immutable Gaussian attributes from mutable
instance state and keep SH degree and color-space metadata explicit. The
renderer may pack to a GPU-friendly layout without making that layout the
long-term asset format. Later training can export PLY or emit the same native
product through a separate producer. New training code must not be required
for rendering to load existing models.

## Rendering decision

Start with a backend-neutral correctness path: CPU frustum rejection and
back-to-front depth sort for one static model, then a frame-safe per-instance
stream and instanced quad draw. Evaluate SH color on the CPU for the active
camera in GS1; retain all SH coefficients in the asset. Stream position,
covariance/scale, opacity, and the resulting color as a bounded per-instance
vertex layout. Streaming every SH coefficient as vertex attributes would
exceed practical attribute budgets. The existing common `PipelineDesc` has
per-instance vertex bindings, `RenderBackend` has frame buffer writes, and
`CommandRecorder` has indexed instanced draws. These make a bounded first path
possible on Vulkan and OpenGL. Full depth sorting must use the active camera
and a deterministic tie-breaker; an unsorted alpha blend is not an acceptance
shortcut. Sorting a million splats each camera frame may be slow; GS1 measures
it rather than promising real-time throughput.

Project each anisotropic covariance into screen space in the vertex shader and
evaluate the elliptical Gaussian in the fragment shader. Evaluate SH color by
view direction during CPU preparation and clamp only at the defined
display/lighting boundary. Use linear HDR and
premultiplied alpha consistently. Reject invalid/behind-camera footprints and
bound quad extent to avoid runaway fill. Depth-test against raster mesh depth
without writing mesh depth; document the per-Gaussian depth approximation.

The current `SceneHdr` target clears when begun and has no depth attachment.
Therefore the first integration should use a separate transparent splat
accumulation target, sample the GBuffer's depth while drawing, and add an
explicit composite pass between DeferredLighting and ToneMap. The graph must
declare GBuffer depth read, splat target write/read, and SceneHdr read plus a
separate composed HDR write version. Do not rebind `SceneHdr` as a second color
pass without proving load semantics or alias an HDR read and write physically.
Screenshot/capture must consume the composed `SceneColor`.

Path tracing writes `SceneHdr` through a different graph branch and does not
provide the raster GBuffer depth as its primary depth source. GS1 therefore
uses a checked raster mode fixture. A later stage must define PT depth/guide
occlusion and accumulation invalidation before enabling splats over PT; report
the mode clearly in runtime stats. The module must never silently turn a PT
frame into a purported mixed-mode result.

## Performance and capability boundary

The common recorder currently has no general compute dispatch or compute
pipeline contract. After GS1 evidence, GS3 may introduce a portable compute
capability for visibility, sorting, and indirect draw if the measured CPU path
needs it. That change needs a current Vulkan consumer, explicit OpenGL support
or a reported fallback, graph buffer hazards, and Graphics-owned retirement.
Consider AMD FidelityFX Parallel Sort as a *candidate* then; it is not a
dependency for the first image. A module-private Vulkan sort is also an option
only if its backend boundary and fallback are explicit. Do not import a whole
CUDA rasterizer or viewer framework.

CUDA is an explicit optional GS3 alternative for NVIDIA devices. CUB can sort
depth keys while Graphics retains the draw buffer; Vulkan external-memory and
semaphore interop or GL registration/map/unmap must be implemented below the
common contract, with same-device checks and frame-safe synchronization. A
CUDA training entry can later produce the same CPU/native model contract.
Neither CUDA nor a CUDA toolkit is required to build or run GS1.

For a future live trainer, define an immutable model snapshot with revision and
completion state. Publish a new revision only at a frame boundary after upload
has completed; retire the previous GPU generation after submitted work is
safe. This training-to-render handoff is an interface requirement, not GS1
implementation work.

## Stages and decisions

| Stage | Result | Gate |
| --- | --- | --- |
| GS0 | Pin format, fixture, camera and reference outputs; audit parser license | One redistributable small PLY fixture and expected field/math values |
| [GS1](.plan/GS1.md) | Optional target, PLY import, CPU sort, raster splat/composite path | Static splat scene captured with mesh occlusion on Debug Vulkan and OpenGL |
| GS2 | Multiple instances, reload/retirement, Editor controls and diagnostics | Move, hide, reload, resize, and failure tests; no stale GPU handles |
| GS3 | Profile then select CUDA/CUB or graphics-compute culling/sort, LOD, compression | Matched RelWithDebInfo quality and timing evidence; no regression of GS1 image |
| GS4 | Define interaction with PT branch | Explicit depth/guide policy and temporal reset evidence before mixed PT claim |
| Future | Training entry | Separate producer of the versioned model contract |

The choice of GraphDeco PLY as initial source does not make PLY the final
streaming format. SOG and ratified `KHR_gaussian_splatting` are future import
candidates after the basic scene/render boundary is proven; see
[references.md](references.md). Source content licenses must be checked
separately from parser and renderer code licenses.
