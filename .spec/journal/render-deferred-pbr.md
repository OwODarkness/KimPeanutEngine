# Render Deferred PBR

- Status: partial (D0–D3 complete; D4–D7 pending)
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

- D4 — directional shadow pass family (`Directional2D` depth job, light-space
  constants, caster filtering, shadow slot binding, debug views; the
  `ShadowDepth` material pass is already reserved).
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
