# Render Deferred PBR

- Status: active (D0–D7 evidence complete; future shadow alternatives deferred)
- Last updated: 2026-09-01
- Spec: [Render Deferred PBR](../specs/render-deferred-pbr.md)
- Parent roadmap: [Deferred PBR Renderer Roadmap](../../docs/render/deferred_pbr/TODO.md)

## 2026-08-29 — D0–D3 foundation

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

## Foundation checkpoint — scope and validation

- Architecture or behavior: common target/pipeline descriptions are now
  caller-driven and validated; backends no longer fill formats implicitly.
  Depth test/write is a function of the pipeline's declared depth format on
  both APIs (a no-depth pipeline must not discard fragments). Later D5.2.1 and
  D5.2.2 corrections moved framebuffer-origin normalization to backend
  viewport/readback boundaries, so fullscreen passes use ordinary back-face
  culling again.
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

## Foundation risks recorded at the D3 checkpoint

- The pre-existing scene sphere was likely also culled on Vulkan by the same
  NDC y-flip at this checkpoint; D5.2.1/D5.2.2 later corrected the shared
  projection and backend viewport ownership.
- GL depth test is now derived from the pipeline depth format, but the
  depth-only smoke is lifecycle-only; real shadow *content* is D4.
- Vulkan readback gated on RGBA8_SRGB; D5 retained RGBA8 capture output while
  HDR capture remains outside the current evidence path.
- Vulkan D24S8 uses `IMAGE_ASPECT_DEPTH` only — fine for D32 targets; fix
  before any stencil target.
- Retirement stays `WaitIdle` + rebuild (conservative for a shared target set);
  per-frame fence-queue retirement deferred until multiple generations race.

## Remaining work

- Future shadow alternatives — true cube resources, multiple punctual jobs,
  variable resolution, caching, and general atlas policy require a new design.

## Documentation and follow-up

- Roadmap: [deferred_pbr_TODO.md](../../docs/render/deferred_pbr_TODO.md) —
  concise stage checklists and acceptance criteria.
- Status: [status.md](../../docs/status.md) — D2 and D3 Done entries.
- Module docs: [graphics_module.md](../../docs/graphics/graphics_module.md) —
  stale swapchain auto-fill claim removed; render-target contract subsection
  added. `docs/render/deferred_pbr_plan.md` remains the authoritative design
  and now carries the G-buffer encodings table, color-space rule, tangent
  convention, and material-V2 constant-block ABI.
- Append dated checkpoints/corrections here as D6.3–D7 land.

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

## 2026-08-31 — D5.4 runtime diagnostic capture routing

- Added the Render-owned `CaptureOutput` target and conditional
  `CaptureViewPass`. SceneColor resolves directly; linear depth, world normal,
  base color, material parameters, and directional-shadow visibility convert
  to RGBA8 sRGB before generic Graphics readback.
- Changed request scheduling so accepted semantic captures remain pending until
  their producer/conversion work is recorded. Readback, synchronization, and
  owned CPU pixels remain backend responsibilities; Runtime still owns path
  validation and PNG encoding.
- Extended `capture.screenshot view=` to all six semantic names and added the
  startup selector `--graphics-api vulkan|opengl` for repeatable live command
  validation. Unknown enum values fail explicitly.
- Targeted tests passed: `RenderPassScheduleTest` 8/8 and the combined
  `RenderCaptureServiceTest|ScreenshotCommandProviderTest` selection 9/9.
  `GraphicsSmoke` built and passed on Vulkan/OpenGL. Live command requests
  produced all six PNGs on each backend; base color, normal, material, and
  depth are upright and visually distinct.
- Vulkan live final color and shadow visibility contain the expected cast
  shadows. OpenGL live final color is over-occluded and its shadow-visibility
  conversion is black on every surface, while the isolated D5 `GraphicsSmoke`
  still shows the correct OpenGL cast shadow. This is now a captured,
  reproducible runtime-only producer/descriptor regression, so comparable live
  final-color evidence and overall D5 completion remain open.
- Reference gate: inspected Godot's renderer-owned copy/debug operations and
  bgfx's backend-owned screenshot requests. KimPeanut adopts the explicit
  Render conversion boundary and keeps native readback execution in Graphics;
  it does not import either renderer's broader graph/cache architecture.

## 2026-08-31 — D5.5 HDR environment background

- Added `scene.environment` bootstrap policy and preloaded the existing
  `HDR_041_Path.hdr` through the normal Asset request path. Render never opens
  the source file directly.
- Extended ImageIO with a linear RGBA32F Radiance decode contract. Asset
  converts to RGBA16F and downsamples panoramas wider than 4096 pixels during
  import, limiting the persistent CPU/GPU footprint while retaining the 2:1
  background panorama.
- Render's texture cache now keys the actual GPU format, so HDR and linear/sRGB
  material interpretations cannot collide. Deferred lighting binds environment
  radiance at set 0/binding 7, reconstructs a world ray for background pixels,
  and writes the sample to `SceneHdr`; the existing tone-map stage remains the
  sole LDR conversion. A black RGBA16F fallback preserves scenes without an
  authored environment.
- Full Debug build passed; CTest passed 171/171; direct Vulkan/OpenGL
  `GraphicsSmoke` passed. The wrapper built the target but hit its known empty
  `Arguments` binding defect before launch, so the executable was run directly.
  Live runtime captures on both APIs visibly show the forest panorama at
  `save/screenshots/validation/d5-5-{vulkan,opengl}.png`.
- The existing runtime-only OpenGL shadow/lighting regression remains visible;
  D5.5 changes only the background path. IBL preprocessing and material
  environment lighting remain deferred.
- Reference gate: Godot's renderer-owned sky/radiance separation and bgfx's
  distinct visible-sky versus irradiance/prefilter inputs support keeping this
  background source separate from future IBL artifacts. Their cubemap caches,
  convolution pipelines, and broader renderer architectures were not adopted.

## 2026-08-31 — D5.6 OpenGL depth-clear state isolation

- Live diagnostic capture encoded receiver depth, sampled shadow depth, and
  visibility independently. OpenGL reconstructed plausible receiver depth but
  sampled zero from the shadow attachment. Binding the G-buffer depth at the
  same texture unit produced valid samples, ruling out descriptor-unit failure.
- Forced producer depths and a discard-only shadow fragment isolated the value
  to the attachment load operation: the target remained zero before useful
  draws. The previous color-only pipeline had disabled `GL_DEPTH_WRITEMASK`,
  and OpenGL applies that write mask to depth clears.
- `OpenglCommandRecorder::BeginRenderTarget` now enables depth writes before a
  depth Clear load-op. Draw-time state remains owned by the next bound pipeline.
  `GraphicsSmoke` deliberately binds a color-only pipeline before the shadow
  target so the stale-state transition remains covered on both APIs.
- Full Debug build passed; CTest passed 171/171; direct Vulkan/OpenGL
  `GraphicsSmoke` passed. Inspected live `SceneColor` and `ShadowVisibility`
  captures at `save/screenshots/validation/d5-6-{vulkan,opengl}-*.png` are
  visually matching and contain both lit and occluded surfaces.
- Reference gate: bgfx's simple-shadow implementation keeps the light transform
  consistent and confines origin/depth differences to backend capability
  crops. That evidence rejected another shader-space flip; the local depth
  probe identified the actual OpenGL load-state leak instead.

## 2026-08-31 — D5.7 environment IBL

- `ResourcePipeline` now turns the Asset-owned RGBA16F equirectangular HDR
  source into three CPU-side derived artifacts: cosine-convolved irradiance,
  GGX roughness-prefiltered radiance, and a split-sum BRDF integration LUT.
  Render uploads and owns the three GPU bindings, keyed by source AssetID,
  resolved format, and explicit derived-artifact variant, so none aliases the
  visible sky texture. The source panorama remains the background binding;
  irradiance, prefiltered radiance, and BRDF LUT use separate bindings.
- The common Vulkan upload path does not correctly populate cube faces or mip
  levels. The prefilter is therefore stored as equal-height roughness bands in
  a 2D equirectangular atlas rather than pretending to be a cube-mip resource.
  The shader blends adjacent bands by roughness and applies the split-sum
  diffuse/specular terms.
- Bootstrap exposes optional non-negative `environment_intensity` (default
  `0.25`). It scales material IBL only; visible HDR sky radiance remains
  unscaled so indirect light does not erase directional-shadow contrast.
- `EnvironmentIblProcessorTest` covers constant-environment convolution,
  roughness-atlas dimensions, finite BRDF output, and invalid-source rejection;
  the texture-cache test locks derived-artifact key separation. Focused tests,
  the Debug engine build, and direct Vulkan/OpenGL `GraphicsSmoke` passed with
  Vulkan validation clean after its matching descriptor fixture was updated.
  Live captures show forest reflections on the metallic gold sphere and
  environment diffuse lighting on both APIs at
  `save/screenshots/validation/d5-7-vulkan-ibl-scene-color-1.png` and
  `save/screenshots/validation/d5-7-opengl-ibl-scene-color.png`.

## 2026-08-31 — D6.1 unshadowed point lighting

- Gameplay publishes an opaque point-light source token. Render resolves it to
  `LightType::Point` without a `ShadowHandle`; deferred lighting evaluates
  inverse-square attenuation with a smooth finite-range cutoff using the
  existing `LightGpuData` type switch.
- The bootstrap scene adds one warm point-light actor. The implementation keeps
  source authoring in Gameplay, light selection and policy in Render, and GPU
  bindings in the existing frame-local lighting path.
- Khronos' [KHR_lights_punctual](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_lights_punctual/README.md)
  recommends this finite-range attenuation form, while Godot's
  [scene shader](https://github.com/godotengine/godot/blob/master/drivers/gles3/shaders/scene.glsl)
  independently uses an inverse-square omni-light falloff with a quartic edge.
  Those references informed the attenuation shape only; no glTF import or
  renderer architecture was adopted.

## 2026-08-31 — D6.2 unshadowed spot lighting

- Gameplay publishes copied position, direction, range, and cone values through
  an opaque source token. Render resolves the source to `LightType::Spot`
  without a `ShadowHandle` and reuses the D6.1 finite-range attenuation.
- Deferred lighting applies squared cosine interpolation between the inner and
  outer cone angles. The bootstrap scene adds one blue spot-light actor.
- The cone model follows Khronos' [KHR_lights_punctual](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_lights_punctual/README.md);
  glTF node import and shadow semantics remain out of scope. `Spot2D` and
  `PointCube` target allocation, shadow filtering, scheduling, and budget policy
  remain open D6.3 work.

## 2026-09-01 — D6.3.1–D6.3.3 bounded spotlight shadows

- Spot source descriptions and the Gameplay component/factory now publish
  copied `casts_shadow` intent. `LightSourceRegistry` creates and retires the
  private generational `ShadowHandle` on spot create/update/disable/destroy;
  Gameplay still receives no resolved identity or target.
- Render selects the first enabled, valid `Spot` record with a `Spot2D` shadow
  identity into binding slot 1. It owns a fixed 1024² sampled D32 `SpotShadow`
  target, builds a stable-up perspective light frustum (2× outer cone, range
  far plane), culls ordinary caster bounds conservatively, and shares the
  existing depth-only pipeline/recorder with the directional job. Inactive
  frames clear the target so a retired source cannot leave stale depth
  shader-visible.
- Deferred constants now include a spot matrix/PCF row set; the unchanged
  96-byte `LightGpuData` stride promotes directional slot 0 and spot slot 1
  only for matching source/shadow/kind/slot tuples. The deferred shader binds a
  separate spot depth sampler and applies independent 3×3 receiver PCF.
  `spot_shadow_depth` and `spot_shadow_visibility` are routed through the
  existing Render conversion target and Runtime command vocabulary.
- Focused C++ syntax checks (`cl /Zs`) pass for changed Render, Gameplay,
  source-registry, target, capture, and command-provider translation units;
  `glslc` passes for deferred-lighting and capture shaders on both API defines;
  `git diff --check` passes. The normal RenderPassScheduleTest build is blocked
  before compilation by MSBuild's Windows SDK probe trying to read the denied
  `C:\Users\17519\AppData\Local\Microsoft SDKs` path. Live Vulkan/OpenGL
  `GraphicsSmoke` and screenshot inspection remain unverified for this slice.
- Reference gate: inspected Filament `ShadowMap::updateSpot` (perspective
  projection from light position/direction, twice the outer cone, range far
  bound) and bgfx `examples/15-shadowmaps-simple/shadowmaps_simple.cpp`
  (explicit depth producer/consumer and backend clip/origin normalization).
  Those patterns support the local fixed typed target and shader normalization;
  their atlas, graph, and broader shadow-cache policies were not adopted.

## 2026-09-01 — D6.3.4 spot-shadow diagnostics and runtime proof

- Added semantic `SpotShadowDepth` and `SpotShadowVisibility` coverage to the
  Render capture service and Runtime `capture.screenshot` vocabulary. Command
  and API documentation now list all eight supported semantic views; the depth
  view is explicitly a converted visualization of the sampled D32 spotlight
  map, not a native attachment export.
- Added focused tests for both spot semantic views: Render resolves each view
  to the conversion target before readback, and Runtime maps both command names
  to the typed `CaptureView` values. `cl /Zs` syntax checks passed for
  `render_capture_service_test.cpp` and `screenshot_command_provider_test.cpp`.
- `glslc` syntax checks passed for `capture_view.frag` and
  `deferred_lighting.frag` with both Vulkan and OpenGL API defines.
- Live Runtime command transport exported and inspection-confirmed these
  captures on both APIs: `d6-3-4-vulkan-spot-depth-1.png`,
  `d6-3-4-vulkan-spot-visibility.png`, `d6-3-4-opengl-spot-depth.png`, and
  `d6-3-4-opengl-spot-visibility.png`; Vulkan SceneColor was also exported as
  `d6-3-4-vulkan-scene-color.png`. Spot visibility is deterministic and shows
  object-shaped occlusion on both backends. Perspective D32 values are
  concentrated close to one for this fixture, so the 8-bit spot-depth
  diagnostic is visually near-white while still exercising the typed
  producer → conversion target → readback path.
- The normal MSBuild test rebuild remains blocked before compilation by the
  host Windows SDK probe (`Microsoft.Cpp.WindowsSDK.props` / denied
  `C:\Users\17519\AppData\Local\Microsoft SDKs`). Existing binaries were not
  used as source-test evidence; D7 retains the full validation-matrix rebuild
  and smoke handoff.

## 2026-09-01 — D6.4 bounded point-shadow atlas

- Point source authoring now carries `casts_shadow`; the source registry owns
  the private generational shadow identity through create/update/disable/destroy.
  Render deterministically selects one valid `PointCube` at binding slot 2.
- Render owns a fixed 1536×1024 sampled D32 atlas containing six 512² 90-degree
  views in the canonical 3×2 layout. The target is cleared once per frame and
  each face records through the existing depth-only pipeline with range-sphere
  and per-face caster culling.
- Deferred lighting and capture conversion bind a separate point-shadow
  constant block and atlas sampler. Dominant-axis face selection, Vulkan/OpenGL
  UV normalization, receiver bias, and tile-clamped 3×3 PCF preserve the
  existing `LightGpuData` stride. Runtime now exposes point depth/visibility
  capture names.
- The bootstrap fixture is point-only for visual isolation: directional and
  spot actors are disabled, while the authored point light is enabled and
  shadowed. This ensures observed shadowing comes only from the point path.
- Corrected point-source resolution to copy the registry-owned shadow handle
  into `LightDesc.shadow`; without that assignment the point light rendered
  unshadowed even though authoring requested shadows.
- For shadow readability, the fixture now sets `environment_intensity` to `0.0`
  and places the point source above/behind the receivers (`y=40`, `z=-80`),
  so occluded footprints project onto the camera-facing floor instead of away
  from the view.
- Focused g++ syntax checks and `glslangValidator` Vulkan/OpenGL checks pass.

### D6.4 verification and handoff

- The reference gate compared the local six-face atlas boundary against
  Filament's six conventional punctual-light views, bgfx's explicit depth
  producer/consumer normalization, and Godot's later cube/atlas alternatives.
  The existing 2D atlas remains the smallest valid cross-backend contract for
  this stage; true cube resources remain deferred.
- Runtime command captures succeeded on the rebuilt engine for both Vulkan and
  OpenGL: `d6-4-rebuilt-vulkan-scene_color.png`,
  `d6-4-rebuilt-vulkan-point_shadow_depth.png`,
  `d6-4-rebuilt-vulkan-point_shadow_visibility.png`, and the corresponding
  `d6-4-rebuilt-opengl-*` files. The earlier deterministic point-only fixture
  captures (`d6-4-vulkan-*` and `d6-4-opengl-*`) were visually inspected for
  multi-face orientation, tile boundaries, caster/receiver placement, and
  cross-backend visibility. Visibility was non-uniform and equivalent between
  APIs; perspective D32 depth is near-white after RGBA8 conversion, so it is a
  routing diagnostic rather than a high-contrast depth visualization.
- Added one-shot Render-owned profiling in `RecordPointShadowPass`: it records
  six per-face draw counts, total draws, empty faces, candidate count, and CPU
  command-recording time on the first successful point pass. The fixed target
  budget is 1536×1024×4 = 6,291,456 bytes (6 MiB). GPU shadow-pass timing is
  unavailable because the current RHI has no timestamp-query path. The short
  rebuilt-runtime capture sessions completed before the lazy point-shadow
  pipeline reached that first-ready-pass log, so numeric per-face/CPU samples
  remain a follow-up measurement; the instrumentation itself is compiled and
  covered by the full build.
- Corrected the standalone deferred graphics smoke fixture to declare and bind
  point-shadow descriptor bindings 12 and 13. Corrected newly-added test
  fixtures to retain command registration tokens and to default spot sources to
  unshadowed where the test name requires it.
- The temporary point-only bootstrap diagnostic setup was removed: normal
  directional, point, and spot bootstrap lighting is restored in
  `runtime_global_context.cpp`, with normal `environment_intensity` restored in
  `config/bootstrap.json`.
- Validation passed sequentially: focused D6.4 CTest selection (37/37), full
  Debug build, complete CTest (188/188), and Vulkan/OpenGL `GraphicsSmoke`.
  The initial non-elevated MSBuild attempts failed before compilation at the
  denied Windows SDK probe; the approved rerun succeeded, so that was an
  environment limitation rather than a source failure.

## 2026-09-01 — Deferred point-shadow resource follow-up

- Ran the rebuilt Engine through the loopback agent transport on both APIs.
  Each session was held for an extended warm period before a second capture:
  Vulkan used `--graphics-api vulkan --agent-port 37373` and OpenGL used
  `--graphics-api opengl --agent-port 37373`; each exported
  `point_shadow_visibility` through `capture.screenshot`.
- The existing one-shot Render profiler reached its first successful point
  pass in both sessions. Vulkan recorded
  `face_draws=[2,3,1,0,0,6]`, `total_draws=12`, `empty_faces=2`,
  `candidates=6`, and `cpu_record_us=307`. OpenGL recorded the same draw
  distribution and counts with `cpu_record_us=177`. The fixed target remained
  `1536x1024 D32`, `6,291,456` bytes (6 MiB). `gpu_time=unavailable` remains
  expected because the RHI has no timestamp-query path.
- Warm Vulkan and warm OpenGL visibility captures were each byte-identical to
  their corresponding earlier capture (`Get-FileHash -Algorithm SHA256`),
  showing stable object-shaped occlusion. Visual inspection found no visible
  3×2 tile seam or face-orientation discontinuity. The backends differed only
  in their expected encoded PNG bytes, not in the observed shadow pattern.
- Decision: retain the six-face 2D atlas as the fixed-budget baseline and close
  the cubemap decision without implementation. The measured six-pass workload
  and observed quality provide no demonstrated correctness, quality, memory,
  or performance benefit that would justify a new cross-backend cube/subresource
  contract. Multiple punctual jobs, caching, variable resolution, and render
  graph work remain outside this decision.
