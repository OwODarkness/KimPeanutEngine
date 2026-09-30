# TP2 — controllable landforms and derived fields

## Goal

Extend TP1's recipe evaluator with deterministic landform controls, physically
sampled derived fields, and finite-domain drainage routing. Keep the reference
implementation headless and CPU based. The user also requested a terrain view
mode in TP2. Its host must compose the shared renderer without game startup.
TP3 continues with authoring controls, preview replacement, diagnostics and bake.

## Boundaries

- Put reusable seeded coherent noise in Core Math, with no Terrain dependency.
- Keep Terrain domain, field and drainage values independent of Asset, Render,
  Graphics and Editor.
- Height samples are elevation offsets above `datum_y_m`; mesh projection
  subtracts that datum and mesh conversion adds it once when building vertices.
- Operate in recipe world coordinates measured in meters. Clamp detail frequency
  to the sample lattice's Nyquist limit; do not normalize chunks independently.
- Keep authored lake masks distinct from depressions discovered in routing.
- Project triangle `MeshData` vertically by taking the highest X/Z surface hit;
  overhangs are lost and an explicit finite no-hit elevation offset is required. The
  terrain recipe source currently covers raster payloads; model-asset decoding
  remains an import adapter task.
- The viewer reuses the shared 3D renderer and is host-selected. It does not load
  a game Level or editor panels. Its fixed fixture is converted to a registered
  mesh Asset and prepared with an explicit render-asset root; live generated
  publication remains TP3. Never add a second renderer.

- Keep PCG evaluation independent of Gameplay. The viewer integration adapter
  reuses a minimal GameplayWorld and existing Actor/component mesh, camera
  and light source lifecycles. No game Level startup is needed for that reuse.
  Prepare/register generated assets before Actor source creation; Actor reuse
  alone cannot publish a missing prepared-catalog record. Add a terrain-specific
  component/factory only when actual instance behavior requires it.

## Operators and algorithms

- `terrain.heightfield.constant`, ridge/valley line influence, height raster
  resampling, domain warp, ridged detail, remap and blend.
- Slope is `atan(length(gradient))`, in radians, using physical X/Z spacing.
- Curvature is the grid Laplacian, in inverse meters, with one-sided second
  order boundary differences (four samples when available).
- Drainage uses deterministic D8 priority flood. Perimeter cells or authored
  lake cells seed routing; first-visit parents form an acyclic forest reaching
  an outlet. Accumulation starts at one sample per cell and is summed in reverse
  flood order. Depression fill is routing state and does not alter authored
  terrain heights.

## Acceptance

- A height raster resamples bilinearly in world coordinates.
- Ridge/valley, warped/ridged, remap and blend outputs are deterministic and
  bounded by explicit parameter validation.
- Plane slope matches `atan(sqrt(dx^2 + dz^2))`; plane curvature is zero within
  float tolerance, including edges.
- Drainage is acyclic, every path ends at a boundary or declared lake, and
  accumulation equals the number of upstream cells.
- Plateau, mountain-basin and coastal-plain recipe fixtures have stable expected
  samples and can use different sample spacing over the same world domain.
- The fixed terrain viewer renders through the existing 3D renderer on Vulkan
  Debug and OpenGL raster. Scene-color captures show the generated terrain on
  both backends. OpenGL sectionless meshes use the full index count, matching
  the shared draw-recorder contract (see journal).

## Validation

Use `kp.ps1 build TerrainGenerationTest`, `kp.ps1 test -l terrain`, and a
normal module-bootstrap build with Terrain disabled. Viewer host-selection
contracts are tested independently. Runtime captures use the approved external
launch path, checked-in fixture, Vulkan validation and OpenGL raster. No
performance claims are made from Debug timing.

## Reference pattern

The local engine-reference study and Godot editor source show an editor tool
composing its preview from engine viewport/camera/light/mesh primitives while
keeping tool code above scene/render services. Map that ownership direction to
the existing KimPeanut host and Render contracts; do not copy implementation.
