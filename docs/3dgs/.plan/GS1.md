# GS1 — First Integrated Gaussian Renderer

[Architecture](../PLANS.md) · [Roadmap](../TODO.md) ·
[References](../references.md)

## Question and outcome

Can one already-trained GraphDeco PLY render as ordered anisotropic splats in
KimPeanutEngine's raster scene, alongside an ordinary mesh, on Vulkan and
OpenGL without importing the original CUDA/SIBR renderer? GS1 is accepted only
with captured frames, mode/validation evidence, and clean lifecycle checks.

## Scope and interfaces

1. **Product:** Optional CMake module target. A PLY importer uses a small
   parser behind a Gaussian-specific adapter, validates named fields and
   counts, then emits an immutable versioned model. Keep source path and
   dependency identity in Asset. The renderer receives a ready model handle,
   never a filename. One scene instance carries transform, visibility, and
   model reference.
2. **Preparation:** Convert source logits/scales and normalize rotation once.
   Preserve SH degree 0–3 and its channel/coefficient order. The CPU artifact
   contains bounds, packed attributes, and a stable primitive ID. At frame
   time cull/reject invalid footprints, evaluate SH color for the camera,
   sort visible IDs back-to-front by camera depth with a stable ID tie-breaker,
   and stream packed position/covariance/opacity/color attributes through
   frame-safe Graphics buffers. Do not stream full SH arrays as vertex inputs.
3. **Projection/draw:** Use a six-index quad with per-instance Gaussian data.
   The shader projects 3D covariance through the camera Jacobian into a
   screen-space conic, receives the camera-evaluated SH color, and evaluates alpha
   over a bounded footprint. Emit premultiplied linear HDR color. Check
   projection handedness, pixel center, alpha cutoff, and matrix/SH layout
   against a small analytic fixture before comparing a trained scene.
4. **Graph:** Add a splat accumulation target/pass and a composite pass to the
   raster branch after DeferredLighting and before ToneMap. Explicitly read
   GBuffer sampled depth; do not write it. Composite `splat + scene*(1-alpha)`
   into a new HDR target/version with correct graph dependencies and target load
   semantics.
   No ad hoc Editor draw or backend object access from the module.
5. **Presentation:** A checked-in startup fixture creates one model and one
   mesh with known front/behind regions, a fixed camera, and a command-visible
   active raster mode. The normal screenshot path captures final SceneColor.

## Ownership and failure invariants

- Asset's immutable payload survives multiple instances; a failed import or
  reimport never publishes a half-valid model.
- Per-frame sort/upload buffers cannot be overwritten while an older submitted
  frame reads them. Graphics owns their allocation and retirement.
- Render graph owns target lifetimes and transitions. The module owns only
  Gaussian pass policy and temporary CPU draw data.
- Empty/offscreen models skip draw and retain the ordinary mesh frame. Import
  failure is diagnostic and does not remove the last ready scene.
- PT mode remains explicitly unsupported for GS1; no splat pass is declared in
  that branch and runtime stats identify the effective mode.

## Acceptance evidence

- **Import tests:** binary little-endian PLY field order and SH count,
  degree 0 and 3, activation, quaternion normalization, bounds, bad header,
  missing field, NaN/Inf, overlarge count. Use small generated fixtures.
- **Math/sort tests:** projected ellipse at fixed camera, positive covariance,
  behind-camera rejection, near/far order, deterministic equal-depth tie.
- **Build/contracts:** SDK-free optional-off configure/build, affected target,
  Graphics/Render contract tests, and Vulkan shader compilation.
- **Runtime:** Debug Vulkan validation enabled and OpenGL with the same fixture
  and camera. Capture final color and check mesh in front, splat in front,
  transparent overlap, camera movement, viewport resize, unload/reload, and
  clean shutdown. Record validation messages and effective RT mode. Use the
  repository's approved outside-sandbox launch and command capture workflow.
- **Timing:** A fresh RelWithDebInfo run records model size, viewport,
  camera-motion window, CPU sort/upload and GPU draw/composite cost. It is a
  baseline for GS3, not a real-time threshold claim.

## Deferred from GS1

CUDA/CUB and GPU radix sort, indirect draw, SOG/SPZ/glTF import, training,
streaming, lighting, shadow casting, collision, multi-model global ordering,
and PT composition require their own measured or semantic gate. CUDA may be
selected in GS3 without changing the GS1 asset contract.
