# Render Graph R4.4 journal — 2026-09-19

## Scope

R4.4 groundwork begins the Graphics-owned native acceleration-structure path
without enabling ray-query rendering. The existing Vulkan/OpenGL raster path
and the explicit fallback remain authoritative until the complete Render
geometry snapshot, descriptor binding, and shader consumer exist.

## Implementation

- Vulkan probes Vulkan 1.2-capable devices for
  `VK_KHR_acceleration_structure`, `VK_KHR_ray_query`,
  `VK_KHR_deferred_host_operations`, `VK_KHR_buffer_device_address`, and the
  required acceleration-structure, ray-query, and buffer-device-address
  features. It enables the set only when all prerequisites are present.
- Device-address-bearing Vulkan allocations are now tracked through the native
  memory pool/dedicated allocators, and geometry buffers carry the build-input
  and shader-device-address usage bits when the device supports the path.
- Added `VulkanAccelerationStructureOwner`, which owns opaque AS handles,
  native storage, BLAS/TLAS build recording, temporary scratch/instance buffers,
  and submission-serial retirement. The recorder translates AS usage into a
  conservative sync2 memory barrier.
- The owner is attached to the Vulkan frame boundary and can expose a built
  TLAS through the existing borrowed backend seam. Pipeline, descriptor-set,
  and shader-side ray-query execution remain unsupported, so the effective
  ray-query capability gate stays false.

## Validation

- `cmake --build build --config Debug --target Graphics` passed.
- `cmake --build build --config Debug` passed after loading Vulkan extension
  commands through `vkGetDeviceProcAddr`.
- Focused CTest `RenderGraph|RenderSystem|Graphics`: 46/46 passed.
- `GraphicsSmoke` passed for Vulkan and OpenGL. Vulkan initialized the RTX 4070
  device with no validation failure; existing geometry-view diagnostic logs and
  foreign-model compatibility warnings remain unrelated baseline output.
- Full CTest completed 967/968; the one failure is the pre-existing
  `LevelLoaderTest.LoadsCheckedInGameplayLevelFixtures` fixture because
  `asset/model/rock1-bl/rock2` is absent from the archive.
- The six retained smoke capture hashes are unchanged from the R4.3 baseline:
  Vulkan/OpenGL final, D2, and D5 remain byte-identical.

## Remaining work

1. Add a Render-owned, frame-stable geometry/instance snapshot and a common
   buffer-resolution seam for the native owner.
2. Schedule the owner build/update as graph work and import its TLAS into the
   same frame plan before selecting the ray-query variant.
3. Add Vulkan descriptor binding and the directional ray-query shader variant;
   only then advertise `SupportsRayQueryShadows()` and collect RT captures and
   per-pass CPU/GPU measurements.
