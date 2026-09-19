# Render Graph R4.2 journal — 2026-09-19

## Status

R4.2 common Graphics RT contract is complete. R4.3 is the next stage for the
native owner and imported-TLAS graph path.

## Changes

- Added opaque generational `AccelerationStructureHandle` and
  `RayTracingPipelineHandle` types.
- Added API-neutral AS geometry/instance/build, RT-pipeline, AS/storage-image
  binding, and dispatch descriptors with validation helpers.
- Added effective capability gates for ray-query shadows and RT pipelines.
  The current OpenGL and Vulkan backends intentionally report the new flags as
  unavailable until a complete native owner exists.
- Added optional RT command-recorder methods whose default is explicit failure;
  existing raster backends and raster resource bindings are unchanged.
- Added the `RayTracingResourceOwner` lifetime seam documenting deferred
  destruction, completed-serial collection, and submission retirement. It is
  not exposed through `RenderBackend` until R4.3.

## Reference mapping

The local Sakura study supports opaque backend-owned handles and a device/owner
boundary separate from frame policy. The local gkNextEngine study supports the
Vulkan-first, capability-gated, evidence-driven fallback policy. No reference
source was copied.

## Validation

- Focused Debug build targets passed:
  `GraphicsContractTest`, `RenderGraphTest`, `RenderSystemTest`, and
  `GraphicsSmoke`.
- Focused CTest regex
  `Graphics|RayTracing|RenderGraph|RenderSubmission|RenderSystem`: 60/60 passed.
- Full Debug build passed with `cmake --build build --config Debug`.
- `GraphicsSmoke.exe`: passed six frames per API; Vulkan reported no validation
  errors, OpenGL retained only its existing intentional geometry diagnostics.
- R4.0/R4.1 capture hashes were preserved:
  - Vulkan final: `7A26D2994861341DFE2F6B81BB99682BFA1F1C4167AC7B92C537DE4122B10F43`
  - OpenGL final: `7AE86480265FDFE1F31F8D1227F3D59A48F9CB58DC245F60C94F3E298BBE386C`
  - Vulkan D2: `828F9F602411A120183AC8EE5FCCCDAC88DE57E139FB6A28527CBAA6C1A1422C`
  - OpenGL D2: `1FF1F3EFB148BE1423C310F69F98AF5F5FAE1B019B4764A41F11833112DE6349`
  - Vulkan D5: `D32FEA4132A221477BA677D238365E312C75B976B989490F1E144551A9046D7`
  - OpenGL D5: `61BBD0EFAAD3FEBA1493477A0A659D5E52A8575CADC6DACA3096787E3A6F8D1E`
- Runtime log: `save/logs/r4-2-smoke.txt`.

## Remaining risk

The R4.2 owner interface is compile-time only. R4.3 must prove native Vulkan
feature probing, AS storage/build/update, descriptor translation, command
recording, retirement, and imported-TLAS fallback before setting the effective
capability flags.
