# Render R4.2 — common Graphics RT contract

- Status: complete
- Owner: Render/Graphics
- Parent TODO: [render graph R4 roadmap](../../docs/render/render_graph/TODO.md)

## Objective

Define the smallest API-neutral Graphics contract needed for the selected
ray-query directional-shadow consumer and the later imported-TLAS slice. The
contract must preserve the Render/Graphics ownership boundary and must not make
the current raster renderer depend on a partially implemented backend.

## Scope

- opaque generational handles for acceleration structures and RT pipelines;
- common descriptors for triangle geometry, BLAS/TLAS capacity, instances,
  build/update requests, RT pipelines, AS/storage-image bindings, and dispatch;
- deterministic descriptor validation helpers;
- effective capability gates that require both the common contract and a
  complete backend owner;
- optional command-recorder operations with explicit unsupported defaults;
- an ownership interface documenting deferred retirement and submission-safe
  scratch reuse without wiring it into Render yet.

## Invariants

1. Common headers contain no Vulkan/OpenGL types, device addresses, build flags,
   shader-binding-table records, or native synchronization values.
2. AS and RT-pipeline handles use the existing generational handle model and
   are opaque to Render.
3. `GraphicsCapabilities` reports effective support only after a backend owns a
   complete implementation; raw extension presence is not sufficient.
4. Raster `ResourceBinding` remains unchanged. RT-only AS and storage-image
   bindings use a separate descriptor variant so existing Vulkan/OpenGL
   descriptor managers cannot silently misinterpret them.
5. Unsupported backends return false from optional RT command operations and
   continue through the selected raster fallback.
6. Native allocation, scratch reuse, retirement, pipeline baking, and dispatch
   are deferred to the Graphics owner and are not exposed through
   `RenderBackend` in R4.2.

## Non-goals

- Vulkan extension enabling or native AS allocation;
- RT pipeline compilation or shader-binding-table construction;
- graph acceleration-structure/storage-image resource kinds;
- imported-TLAS binding or a ray-query lighting pass;
- changes to assets, gameplay, editor, or the existing raster pass order.

## Acceptance

- [x] Common AS and RT-pipeline handles are typed, opaque, and generational.
- [x] Geometry, build/update, pipeline, binding, and dispatch descriptors have
  deterministic validation helpers.
- [x] Capability helpers reject incomplete common/backend combinations.
- [x] Optional recorder operations fail explicitly when unsupported.
- [x] Focused Graphics/Render tests, Debug compilation, and Vulkan/OpenGL smoke
  pass without changing the raster captures.

## Validation plan

```powershell
cmake --build build --config Debug --target GraphicsContractTest RenderGraphTest RenderSystemTest GraphicsSmoke
ctest --test-dir build -C Debug -R "Graphics|RayTracing|RenderGraph|RenderSubmission|RenderSystem" --output-on-failure
.\build\engine\example\graphics\Debug\GraphicsSmoke.exe *> save\logs\r4-2-smoke.txt
```

The full suite is not part of this contract-only gate because R4.2 does not
change unrelated modules or runtime behavior; the existing repository-wide
fixture failure remains outside this stage.
