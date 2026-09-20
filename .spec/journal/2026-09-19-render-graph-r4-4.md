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

## 2026-09-20 scheduled-build slice

- Render now snapshots visible shadow-casting mesh proxies at the frame
  boundary, resolves immutable triangle inputs through the common backend seam,
  and derives a stable instance signature from mesh handles and transforms.
- Graphics remains the physical owner: Vulkan resolves mesh storage to opaque
  `BufferHandle` descriptors, owns BLAS/TLAS storage, and keeps scratch and
  instance buffers private to the owner.
- The graph now has SSA acceleration-structure writes and separate optional
  BLAS/TLAS build passes. A new or changed mesh schedules BLAS build; changed
  transforms or instance membership schedule TLAS update/build. Unchanged
  frames do not visit either build pass.
- The graph provider maps the logical geometry group to physical buffers only
  while applying the pass transition. Render never sees a device address or a
  Vulkan object.
- Added a compatibility test covering AS version lineage and the BLAS-to-TLAS
  build dependency.

## 2026-09-20 ordering fix and validation

- The first live Vulkan run exposed that build passes authored after the
  external terminal could be scheduled after it when ray-query consumption was
  disabled. The terminal now depends on the active TLAS build, or BLAS build
  when no TLAS is scheduled, and the compatibility test locks the terminal to
  the final compiled position.
- Focused render/graphics CTest passed 30/30; the full Debug build passed.
- Full CTest reached 969 tests: 968 passed and the unrelated checked-in-level
  fixture failed because `asset/model/rock1-bl/rock2` is absent from the
  archive. The same failure reproduces in isolation as
  `LevelLoaderTest.LoadsCheckedInGameplayLevelFixtures`.
- Vulkan startup with `level/sponza.level` passed the live command gate after
  the fix. Performance stats preserved graph compile time and per-pass CPU/GPU
  measurements; scene, world-normal, and linear-depth captures were exported
  under `save/screenshots/validation/`. No terminal-order or Vulkan validation
  errors were found in the run log.
- OpenGL startup remained alive at sustained CPU for roughly two minutes
  without opening its command port or producing readiness logs. It was stopped
  without a capture; OpenGL runtime evidence remains unverified.

## Remaining work

1. Add Vulkan descriptor binding and the directional ray-query shader variant;
   only then advertise `SupportsRayQueryShadows()` and collect RT captures and
   per-pass CPU/GPU measurements.
2. Validate native build/update, resize, orderly close, and lifetime retirement
   on an RT-capable Vulkan device; investigate the OpenGL startup-path blocker
   before claiming cross-API runtime evidence.
