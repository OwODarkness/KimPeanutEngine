# Render Graph R4.0 journal — 2026-09-19

## Status

R4.0 is complete. R4.1 is the next gate. This stage selected the first
ray-tracing consumer and established a validation-clean raster baseline; it did
not add a ray-tracing API or execute ray tracing.

## Decision and scope

The first consumer is ray-query hard-shadow visibility for the directional-light
contribution. It reads the imported acceleration-structure scene snapshot and
directional-light ray policy, produces one visibility result consumed by
deferred lighting, and is invalidated when the participating scene snapshot or
camera changes. The existing directional shadow map remains the fallback;
OpenGL always uses it, and Vulkan uses it when the complete ray-query capability
set is unavailable.

R4.0 keeps Render responsible for policy and pass declarations. Graphics/RHI
retains ownership of acceleration structures, native objects, synchronization,
and release safety. No common contract exposes Vulkan or OpenGL types.

## Implementation and corrections

The Vulkan debug messenger now records the latest validation error behind the
API-neutral `RenderBackend` diagnostic query. `GraphicsSmoke` performs orderly
cleanup first, then fails if Vulkan recorded a validation error. The smoke
fixture also declares the sampled transitions for the D2 and D5 attachment
consumers, and Vulkan translation rejects a pipeline that omits a target depth
format when dynamic rendering binds a depth attachment.

The first validation run intentionally exposed three baseline defects: a
dynamic-rendering depth-format mismatch, missing sampled-image layout
transitions for D2/D5 resources, and the resulting Vulkan validation records.
Those were corrected by declaring the transitions and carrying D32 through the
streaming pipeline description. This confirms that the new failure gate is
observing real backend validation rather than only checking process success.

## Evidence

- `cmake --build build --config Debug --target GraphicsSmoke`: passed after
  running with a sanitized process environment that removes the duplicate
  case-insensitive `PATH`/`Path` variable that otherwise makes MSBuild fail
  with MSB6001 before compilation.
- `cmake --build build --config Debug`: passed.
- `ctest --test-dir build -C Debug -R "RenderGraph|RenderSubmission|Graphics"
  --output-on-failure`: 39/39 passed.
- `GraphicsSmoke.exe`: passed six frames per API and ended with
  `Graphics smoke (6 frames/API): passed`. The final Vulkan log contains no
  `VulkanDeviceLog: [Error]` record. The OpenGL log retains its existing
  command-recorder rejection diagnostics for intentionally invalid geometry
  cases, while the fallback capture completes successfully.
- Preserved captures:
  `save/screenshots/validation/graphics-smoke-vulkan.png`,
  `graphics-smoke-opengl.png`, `graphics-smoke-d2-vulkan.png`,
  `graphics-smoke-d2-opengl.png`, `graphics-smoke-d5-vulkan.png`, and
  `graphics-smoke-d5-opengl.png`.
- Capture SHA-256 values are preserved here for later comparison:
  - Vulkan final: `7A26D2994861341DFE2F6B81BB99682BFA1F1C4167AC7B92C537DE4122B10F43`
  - OpenGL final: `7AE86480265FDFE1F31F8D1227F3D59A48F9CB58DC245F60C94F3E298BBE386C`
  - Vulkan D2: `828F9F602411A120183AC8EE5FCCCDAC88DE57E139FB6A28527CBAA6C1A1422C`
  - OpenGL D2: `1FF1F3EFB148BE1423C310F69F98AF5F5FAE1B019B4764A41F11833112DE6349`
  - Vulkan D5: `D32FEA4132A221477BA677D238365E312C75B976B989490F1E144551A9046D7`
  - OpenGL D5: `61BBD0EFAAD3FEBA1493477A0A659D5E52A8575CADC6DACA3096787E3A6F8D1E`
- The runtime log is retained at `save/logs/r4-smoke.txt`; neither generated
  artifact is committed.

The full `ctest --test-dir build -C Debug --output-on-failure` run completed
with 959/960 tests passing. The one failure is the existing checked-in fixture
failure in
`LevelLoaderTest.LoadsCheckedInGameplayLevelFixtures`: `pbr_showcase.level`
references the absent archived model `model/rock1-bl/rock2`. This is outside
the R4.0 render changes and is reported as an existing repository blocker.

## Follow-up

R4.1 must complete graph correctness prerequisites and the cross-module review
before any RT resource or pass API is introduced. Later stages must discover
the Vulkan acceleration-structure/ray-query prerequisites and preserve the
shadow-map fallback on unsupported hardware.
