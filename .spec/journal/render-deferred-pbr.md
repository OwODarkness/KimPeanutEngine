# Render Deferred PBR

- Status: partial (D0–D4 complete; D5–D7 pending)
- Date: 2026-08-29
- Spec: [Render Deferred PBR](../specs/render-deferred-pbr.md)
- Parent TODO: [Deferred PBR TODO](../../docs/render/deferred_pbr_TODO.md)

## What was done

- **D0 — prerequisites.** Split backend scheduling/ownership from
  `CommandRecorder` implementation (Vulkan/OpenGL own short-lived recorders).
  Kept render-target readback in backend-owned services behind a borrowed
  common interface. Preserved the unlit `ScenePass` baseline throughout.
- **D1 — light source, snapshot, shadow ABI.** Gameplay gained
  `DirectionalLightComponent` + `CreateDirectionalLightActor`, publishing
  coalesced value-only source state through `ILightSourceSink`. Render resolves
  source commands at `RenderSystem::BeginFrame` into private `LightHandle`
  values and an immutable `LightWorld` snapshot (`LightSourceRegistry` owns the
  game-thread inbox; Gameplay keeps only the opaque source token). `LightDesc`
  validates directional/point/spot payloads and optional `ShadowHandle`;
  `ShadowJobDesc` + `IsShadowKindCompatible` prevent incompatible light/shadow
  pairing. `FrameLightingBinding` uploads a 64-record, version-1 uniform array
  at set 0/binding 4 for the active frame (unlit pipeline does not declare it
  yet; D5 attaches it). Runtime composes one default directional `LightActor`.
- **D2 — attachment-capable common graphics contract.** `RenderTargetDesc`
  now carries `std::vector<RenderTargetColorAttachment>` + optional
  `RenderTargetDepthAttachment` (load/store ops, clear values, opt-in sampled
  depth). `PipelineDesc` semantics relaxed: empty color formats = depth-only
  (legal), `TEXTURE_FORMAT_UNKNOW` depth = no depth (legal); reject only when
  both absent. Removed the Vulkan swapchain auto-fill of attachment formats.
  Shared `ValidateRenderTargetDesc` /
  `ValidateRenderTargetPipelineCompatibility` gates target/pipeline agreement.
  Vulkan dynamic rendering builds N `VkRenderingAttachmentInfo`
  (color/depth/depth-only); depth test/write now derives from the pipeline's
  declared depth format. OpenGL creates N `GL_COLOR_ATTACHMENT{i}` FBO
  attachments with `glDrawBuffers` / `glDrawBuffer(GL_NONE)` and derives
  `GL_DEPTH_TEST` the same way. `RenderBackend` gained
  `GetRenderTargetColorAttachment(index)` / `GetRenderTargetDepthAttachment`
  (no native handles above Graphics). Render owns `RendererFrameTargets`
  (named `RenderTargetName::SceneColor` → `render::RenderTarget`, extent/format
  policy in `BuildDesc`, `RebuildForExtent` with `WaitIdle`, `Cleanup`);
  `RenderSystem` runs through it for init/capture/extent/pass/shutdown.
- **D3 — Material Asset V2 + opaque PBR G-buffer.** Material assets version to
  2 (`standard_pbr` shading model; V1 unlit still loads, `standard_pbr` is
  rejected in V1, version 3 rejected). `MaterialAssetResolver` builds the
  canonical 10-param `StandardPbr` template (base_color vec4@0, metallic f@16,
  roughness f@20, occlusion f@24, emissive vec4@32; samplers at bindings
  2/5/6/7/8, binding 4 reserved for D5) with per-texture color-space intent
  (`Srgb`/`Linear`; texture cache key `{asset_id, color_space}`) and
  texture-wins-over-scalar normalization, injecting the two new 1×1 default
  PNGs (`default_white`, `default_flat_normal`) when a texture is absent. The
  tangent audit landed the canonical 5-attribute layout (56-byte
  `data::Vertex`, Assimp `CalcTangentSpace`), world TBN
  `transpose(inverse(mat3(model)))` with `mat3(T,B,N)` / `rgb*2-1`, and a
  tangent-degenerate guard. G-buffer = 3 color + D32: albedo RGBA8_UNORM
  (linear, clear {0,0,0,0}), normal RGBA16F (raw world-space, {0,0,1,0}),
  material RGBA8_UNORM (metallic R/roughness G/occlusion B, A=1, {0,1,1,0}),
  D32 clear 1.0. `MaterialPass::{Scene, ShadowDepth, GBuffer}` threads through
  `FindMaterialPipeline(handle, pass)`, `CreateMaterialBinding`, `RecordMeshProxy`,
  and the draw-list builder; the pipeline-cache key folds the pass.
  `RenderSystem` schedules `GBufferPass` (opaque draw lists) →
  `GBufferDebugViewPass` (fullscreen composite `albedo*(0.4+0.6*abs(normal.z))`
  into SceneColor) → `EditorCompositePass`, replacing the unlit `ScenePass`
  (unlit stays green in smoke). `RenderCamera` gained near/far setters; the
  engine scene camera frames the ~165-unit rock at far=2000, near=1.

## What changed

- Architecture or behavior: common target/pipeline descriptions are now
  caller-driven and validated; backends no longer fill formats implicitly.
  Depth test/write is a function of the pipeline's declared depth format on
  both APIs (a no-depth pipeline must not discard fragments). Fullscreen
  passes must use `cull_mode = NONE` — the shared fullscreen triangle is
  front-facing in OpenGL's y-up NDC but back-facing in Vulkan's y-down NDC,
  so a back-culling pipeline culls it on Vulkan.
- Important files/modules:
  - Common: `graphics/backend/common/render_target.{h}`,
    `render_target_validation.{h,cpp}`, `pipeline_validation.cpp`,
    `core/base/graphics_type.h` (`TEXTURE_FORMAT_RGBA16F`).
  - Vulkan: `vulkan_backend.cpp`, `vulkan_render_target_manager.{h,cpp}`,
    `vulkan_pipeline_manager.{h,cpp}`, `vulkan_command_recorder.cpp`.
  - OpenGL: `opengl_backend.cpp`, `opengl_pipeline.{h,cpp}`,
    `opengl_command_recorder.cpp`.
  - Render: `render/renderer_frame_targets.{h,cpp}`, `render/render_target.*`,
    `render/render_system.{h,cpp}`.
  - Gameplay/light: `component/directional_light_component.*`,
    `factory/directional_light_actor_factory.*`, `render/light/` registry.
  - Material V2: `asset/material.{h}` + `asset/material_loader.cpp` (version 2,
    `StandardPbr`), `render/material/material_asset_resolver.{h,cpp}`,
    `render/material/material_system.{h,cpp}`,
    `render/render_resource_resolver.{h,cpp}` (nested pass map, per-texture
    color-space cache key), `render/frame_context.{h,cpp}` (pass threading),
    `render/render_world/scene_draw_list.{h,cpp}` (opaque GBuffer filtering).
  - Passes: `render/render_system.{h,cpp}` (`RecordGBufferPass`,
    `RecordGBufferDebugViewPass`), `render/render_pass.{h,cpp}` (`GBuffer`),
    `render/renderer_frame_targets.{h,cpp}` (`RenderTargetName::GBuffer`),
    `render/render_camera.{h,cpp}` (near/far setters).
  - Fixture: `asset/shader/pbr_gbuffer.{shader,vert,frag}` +
    `gbuffer_debug_view.{shader,vert,frag}`, `asset/material/rock_pbr.material`,
    `asset/texture/default/default_white.png` + `default_flat_normal.png`,
    `config/bootstrap.json` (rock scene; PBR shader loads via the material's
    `shader` field, not the warm-up list).
  - Smoke: `example/graphics/rhi_example.cpp`, `asset/shader/multi_output.*`,
    `sample_color.*`.
- Public API or ownership changes: `RenderTargetDesc` shape changed (multi
  color + optional depth); `RenderTarget::Initialize` takes a desc; pipeline
  `depth_attachment_format` may be UNKNOW; Render owns the named target set via
  `RendererFrameTargets`. No native Vulkan/OpenGL type leaks above Graphics.

## Validation

- Required level: L2 (contract/headless) + L3 (GraphicsSmoke both APIs).
- Command: `cmake --build build --config Debug` — PASS (whole tree).
- Command: `ctest --test-dir build -C Debug` — PASS, 148/148 tests
  (MaterialLoaderTest V2 loads + V1 green + version 3 rejected,
  MaterialAssetResolverTest, MaterialSystemTest, RenderPassScheduleTest GBuffer
  schedule, GraphicsContractTest).
- Command: `GraphicsSmoke` Vulkan + OpenGL — PASS. D2 block proves
  multi-attachment (RGBA8 + RGBA16F + D32) → sampled readback → depth-only
  lifecycle on both APIs. D3 block proves the full V2 path: real
  `rock_pbr.material` resolves → GBuffer pipeline → 3-color+depth write →
  debug composite → `save/screenshots/validation/graphics-smoke-d3-{vulkan,opengl}.png`.
  Evidence: both 1600×1024 PNGs ~818 distinct sampled colors, center-region
  non-dark fraction ~0.52 (vulkan) / ~0.50 (opengl), silhouette IoU 0.63,
  `Graphics smoke (3 frames/API): passed`, no Vulkan validation errors.
- Command: launch `KimPeanutEngine` — PASS. Engine boots (Vulkan), bootstrap
  drains the rock material/model/textures + the 2 default PNGs, the PBR and
  debug-view shaders process at runtime, and the log is free of Vulkan
  validation errors. (One pre-fix boot error — the debug composite drew into
  SceneColor, which still carried the retired ScenePass's D32 depth, against a
  no-depth pipeline — was fixed by dropping SceneColor's depth; it is a final
  color target now.)
- Debug note: the D2 sampled output was fully black on Vulkan until the root
  cause was isolated — depth-test-derived-vs-attachment (fixed, still black),
  sampling/descriptor path (correct), then a blue-clear diagnostic proved the
  output+readback chain works and the sample pass simply culled the fullscreen
  triangle (NDC y-flip above). Fixed with `cull_mode = NONE`. D3 reused that
  proven `cull_mode = NONE` fullscreen pattern for the debug composite.

## Remaining risks and unverified areas

- The pre-existing scene sphere is likely also culled on Vulkan by the same
  NDC y-flip; the C4 screenshot check only requires varied pixels (the plane
  renders), so it was masked. Must be confirmed when D5 lighting is inspected.
- GL depth test is now derived from the pipeline depth format, but the
  depth-only smoke is lifecycle-only; real shadow *content* is D4.
- Vulkan readback gates on RGBA8_SRGB — D5 HDR capture needs a readback format
  extension.
- Vulkan D24S8 uses `IMAGE_ASPECT_DEPTH` only — fine for D32 targets; fix
  before any stencil target.
- Retirement stays `WaitIdle` + rebuild (conservative for a shared target set);
  per-frame fence-queue retirement deferred until multiple generations race.

## Remaining work

- D5 — `DeferredLightingPass` + `ToneMapPass` to LDR SceneColor, capture views,
  PBR screenshot evidence on both APIs.
- D6 — unshadowed point/spot, `Spot2D`/`PointCube` jobs, shadow budget policy.
- D7 — evidence and handoff (contract coverage, validation matrix, screenshots,
  ledger/status updates).

## Documentation and follow-up

- Ledger: [deferred_pbr_TODO.md](../../docs/render/deferred_pbr_TODO.md) — D0,
  D1, D2, D3 marked done with landing notes (D3 includes the tangent-audit
  result).
- Status: [status.md](../../docs/status.md) — D2 and D3 Done entries.
- Module docs: [graphics_module.md](../../docs/graphics/graphics_module.md) —
  stale swapchain auto-fill claim removed; render-target contract subsection
  added. `docs/render/deferred_pbr_plan.md` remains the authoritative design
  and now carries the G-buffer encodings table, color-space rule, tangent
  convention, and material-V2 constant-block ABI.
- Append dated checkpoints/corrections here as D4–D7 land.

## 2026-08-30 — D4 directional shadow depth family

- Gameplay exposes only `casts_shadow`; Render resolves it into a private,
  generational `ShadowHandle` and retires it when the source disables or dies.
  `RenderSystem` selects one enabled directional job from the immutable
  snapshot, with a camera-centred 100×100 orthographic fit and 200-unit depth
  range. It records the job before `GBufferPass` into a fixed 2048² D32 target
  with opt-in sampled depth, filtering `visible && casts_shadow && opaque &&
  ready` proxies. Its binding slot is render-private and reserved for D5.
- The generic depth override uses only `PerPassData` and `PerObjectData` at
  set 0/bindings 0 and 1. No Graphics contract changed and no native type or
  texture escapes to Gameplay/Asset. No portable common depth-bias state
  exists, so the pass deliberately does not claim an API-specific bias policy.
- `GBufferDebugViewPass` converts the sampled D32 map into a fourth SceneColor
  panel, preserving the existing capture path. An unscheduled job leaves clear
  depth and records no shadow pass.
- Validation: `RenderPassScheduleTest` 7/7 passed. `GraphicsSmoke` passed on
  Vulkan and OpenGL after it was extended to write, sample, capture, resize,
  and tear down a real depth-only sampled target. The wrapper's `smoke` command
  builds successfully but cannot invoke a zero-argument executable because of
  its existing empty-array parameter bug; the built executable was run directly.
- Reference gate: inspected gkNextEngine's `ShadowMapPass.cpp` (depth-only
  D32 producer followed by shader-readable consumption) and Godot's
  `render_scene_buffers.h` (renderer-owned buffer configuration). KimPeanut
  adopts only producer/consumer intent and Render ownership, rejecting native
  Vulkan framebuffers, global descriptors, and cascades.

## 2026-08-30 — D5.1 HDR presentation spine

- Added Render-owned sampled RGBA16F `SceneHdr`; `SceneColor` remains the
  stable RGBA8 sRGB capture/editor output. Replaced fragile manual target and
  pass-resource counts with enum sentinels. This also corrected the stale D4
  target count that excluded `DirectionalShadow` from allocation/access.
- The fixed schedule now validates `ShadowDepthPass -> GBufferPass ->
  GBufferDebugViewPass(SceneHdr writer) -> ToneMapPass(SceneColor writer) ->
  EditorCompositePass`. The debug producer is intentionally temporary;
  Cook-Torrance lighting and shadow-factor evaluation remain separate D5 work.
- `ToneMapPass` samples SceneHdr and applies global Reinhard in linear space;
  the SceneColor attachment performs final sRGB encoding. Fullscreen mesh and
  sampler lifetime stay owned by `RenderSystem`; targets stay owned by
  `RendererFrameTargets`; no common Graphics contract or native type changed.
- Validation: `RenderPassScheduleTest` passed 8/8. `GraphicsSmoke` passed on
  Vulkan and OpenGL after its PBR block was extended through RGBA16F SceneHdr
  and tone mapping. Both inspected 1600x1024 captures are non-uniform and show
  the same four diagnostic regions and rock silhouette at
  `save/screenshots/validation/graphics-smoke-d5-{vulkan,opengl}.png`.
- Reference gate: Godot's current `renderer_scene_render_rd.cpp` keeps internal
  scene color separate and invokes a distinct tone-map operation into the
  display destination. KimPeanut adopts that HDR/LDR separation but keeps its
  existing fixed schedule and simple extent-owned target set, rejecting the
  dynamic post-effect/cache hierarchy.

## 2026-08-30 — D5.2 directional Cook-Torrance lighting

- Replaced the scheduled G-buffer diagnostic producer with
  `DeferredLightingPass`: sampled albedo/normal/material/D32 plus frame light
  data and inverse view-projection constants now produce `SceneHdr`, followed
  by the existing tone-map and editor composite passes. The debug pipeline is
  retained but unscheduled for future explicit capture routing.
- G-buffer D32 is shader-readable. Depth reconstruction uses OpenGL's [-1,1]
  and Vulkan's [0,1] NDC depth conventions without adding an RHI API branch.
  The shader evaluates GGX distribution, Smith geometry, and Schlick Fresnel
  for enabled directional records. Point/spot and shadow contribution remain
  explicit later slices.
- Locked the version-1 light std140 ABI with exact `LightGpuData` size/offset
  assertions and added an 80-byte deferred-camera constant block. No native API
  type or common Graphics contract changed.
- Validation: `RenderPassScheduleTest` built and passed 8/8. `GraphicsSmoke`
  built and passed Vulkan/OpenGL when invoked directly; `kp.ps1 smoke` still
  has its pre-existing zero-argument binding failure. Both final captures are
  non-uniform and exceed the direct-light threshold. The full Debug build and
  complete CTest suite also passed (155/155) because the shader-facing render
  ABI header is shared by multiple consumers.
- Visual inspection found a cross-backend normal-map appearance mismatch:
  Vulkan is broadly front-lit and OpenGL mostly grazing-lit. Diagnostic renders
  confirmed identical light UBO values but different sampled world normals.
  Treat this as a D3 normal-convention follow-up before D5.3 shadow comparison.
- Reference gate: bgfx's reflective-shadow-map example reconstructs world
  position from sampled depth with an inverse view/projection transform;
  Filament likewise documents inverse-projection world-position reconstruction
  and explicit clip-space convention handling. KimPeanut adopts that narrow
  pattern while keeping its existing frame target and fixed schedule ownership.

## 2026-08-30 — D5.2.1 cross-backend winding correction

- Root cause: `RasterState::front_face` was translated to the identically named
  Vulkan enum while Vulkan used a positive-height viewport. Vulkan's upper-left
  framebuffer coordinates reverse engine/OpenGL y-up winding, so back-face
  culling selected the opposite closed-mesh surface. The normal map and TBN
  were not API-dependent.
- Fixed the owning boundary in `ConvertToVulkanFrontFace`: common clockwise and
  counter-clockwise semantics are swapped only during Vulkan translation.
  Negative viewport height was rejected because it would also change the
  established render-target/readback orientation; shader normal/UV flips were
  rejected because they would hide a raster-state error.
- Removed the fullscreen `cull_mode = NONE` workaround from D2/D5 smoke and
  RenderSystem fullscreen passes. Their shared CCW triangle now renders with
  back-face culling on both APIs, making the smoke an execution-level winding
  regression check.
- Diagnostic normal captures had identical bounds/means and differed at only
  one pixel by 1/255 in one channel. Production final-lighting captures likewise
  have identical bounds/means and differ at one pixel by 1/255 in two channels.
- Validation: `GraphicsSmoke` passed for Vulkan and OpenGL with back-face
  culling restored; the full Debug build passed and the complete CTest suite
  passed (155/155). `git diff --check` found no whitespace errors.
- Reference gate: the Vulkan specification defines facing from signed area in
  framebuffer coordinates and defines framebuffer origin as upper-left; the
  Vulkan viewport reference documents negative height as the alternative Y
  normalization. bgfx was inspected as cross-API winding precedent, but its
  broader state model was not imported.

## 2026-08-30 — D5.2.2 projection/viewport ownership correction

- Corrected D5.2.1 after live inspection showed OpenGL rendering the closed
  rock from its interior. The shared perspective matrix still negated Y, so
  OpenGL received Vulkan-oriented clip coordinates and culled the exterior.
- Projection math now remains y-up, matching the orthographic matrix. Vulkan
  owns its framebuffer-origin conversion with `y = height` and negative
  viewport height in both default render-target recording and explicit
  `SetViewport`; its front-face enum translation is direct after that change.
- OpenGL readback now reverses its native bottom-up rows before publishing the
  common top-left `CapturedImage`. Final Vulkan/OpenGL captures show the same
  exterior silhouette, orientation, material detail, and lighting.
- Regression evidence: a render-camera unit assertion locks positive-Y
  perspective/orthographic projection, fullscreen back-face culling remains
  enabled in `GraphicsSmoke`, and the smoke now rejects divergent Vulkan/OpenGL
  final-image silhouettes. Both API smoke paths pass. The targeted
  projection/schedule tests passed 12/12; the full Debug build and complete
  CTest suite passed 165/165; `git diff --check` reported no whitespace errors.
- Reference gate: Vulkan specifies polygon facing from framebuffer-coordinate
  area and documents negative viewport height as the mechanism for negating
  clip-space Y. The fix adopts only that backend normalization boundary.

## 2026-08-30 — D5.3 directional shadow consumption

- Added a frame-local resolved-shadow value. `BuildLightGpuFrameData` promotes
  only a matching source light, authored shadow generation, and compatible
  shadow kind; stale/mismatched inputs stay unshadowed. The existing light ABI
  size and point/spot records are unchanged.
- `DeferredLightingPass` now declares `DirectionalShadow` as a read, binds its
  sampled D32 texture through a Render-owned clamp sampler, uploads the light
  view-projection and bias/texel constants, and applies manual 3x3 PCF to direct
  directional light. Ambient remains unshadowed. Receiver-side bias is the
  explicit interim policy because common `PipelineDesc` has no depth bias.
- Cross-API visual diagnosis found that exact shadow lookup requires both
  producer and reconstruction depth conventions to agree. Vulkan G-buffer and
  shadow vertex stages translate shared `[-W,+W]` clip depth to `[0,+W]`;
  deferred reconstruction maps depth back and handles Vulkan's negative
  viewport Y for screen and offscreen coordinates.
- Extended `GraphicsSmoke` with the supplied brick floor as a receiver and the
  rock as the sole caster. Direct smoke execution passed Vulkan and OpenGL;
  inspected 1600x1024 captures show matching cast shadows and the automated
  silhouette comparison passes. `kp.ps1 smoke` still stops before launch due
  to its pre-existing empty-array `Invoke-External` binding defect.
- Corrected the live fit after checking the Runtime camera path: the fixed
  orthographic volume now follows a point 300 units along camera forward with
  a 150-unit half extent and 600-unit depth range, covering the bootstrap scene
  rather than centering empty space at the eye.
- Added narrow `.gitignore` exceptions for the live G-buffer, diagnostic, and
  directional-shadow shader programs. Those D3/D4 sources were present and
  executed locally but still matched `/asset/shader/*`; without the exceptions,
  a normal change set would silently omit D5.3's Vulkan producer fixes.
- Targeted validation: `RenderPassScheduleTest` built and passed 8/8 before the
  visual run. Reference gate: Filament's shadow path informed the frame-local
  light transform and explicit bias policy; Vulkan's official depth guidance
  supports the clip-depth translation. Cascaded fitting and backend-specific
  raster bias were intentionally not imported.
- Final validation after the live-fit correction: the full Debug build passed,
  complete CTest passed 168/168, focused light/schedule tests passed 11/11,
  direct Vulkan/OpenGL `GraphicsSmoke` passed, both captures were inspected,
  and `git diff --check` reported no whitespace errors.
