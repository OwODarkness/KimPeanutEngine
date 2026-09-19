# Render R4.3 — graph vocabulary and imported TLAS

- Status: graph slice complete; native owner remains capability-gated
- Owner: Render/Graphics
- Parent: [R4 plan](../../docs/render/.plan/R4.md)

## Objective

Connect the API-neutral ray-query consumer to the render graph without moving
acceleration-structure ownership into Render or enabling an incomplete backend.

## Changes

- Added a typed `GraphAccelerationStructureHandle` and imported-AS graph
  resource records.
- Added AS build-input/output/read and storage-read/write usage vocabulary plus
  portable shader-stage intent.
- Added a ray-query frame-plan variant that imports `SceneTLAS` and declares its
  read on deferred lighting at `RayTracingShader` stage.
- Added an explicit frame-local logical-to-physical TLAS binding. The physical
  handle is borrowed from `RenderBackend`; the graph never creates or destroys
  it.
- Added Graphics AS transition requirements and a borrowed active-TLAS seam.

## Invariants

1. The graph contains no Graphics-native or API-native types.
2. A ray-query plan is selected only when the effective capability gate and a
   valid backend-provided TLAS are both present.
3. OpenGL and the current Vulkan backend retain the authored directional
   shadow-map fallback because neither advertises a complete native owner.
4. AS construction/update is not scheduled by the graph; that remains R4.4.
5. Unrelated Asset, Gameplay, and Editor work is outside this stage.

## Acceptance

- [x] Imported AS handles compile through the graph lifetime and transition
      machinery.
- [x] The ray-query variant declares the TLAS read and ray shader stage.
- [x] Storage usage vocabulary is validated by graph tests.
- [x] Missing physical TLAS bindings fail explicitly.
- [x] Existing raster plan and fallback behavior remain unchanged.
- [ ] Native Vulkan owner and runtime ray-query execution are not claimed until
      the complete extension/feature/owner path is implemented.

## Validation

```powershell
cmake --build build --config Debug --target RenderGraphTest RenderSystemTest GraphicsContractTest
ctest --test-dir build -C Debug -R "RenderGraph|RenderSystem|Graphics" --output-on-failure
```
