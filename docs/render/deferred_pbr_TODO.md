# Deferred PBR Renderer TODO

**Status: proposed.** Execution ledger for the
[Deferred PBR Renderer Plan](deferred_pbr_plan.md) and its
[implementation spec](../../.spec/specs/render-deferred-pbr.md). This is a
fixed ordered-pass migration, not a render-graph proposal.

## D0 — completed prerequisites

- [x] Keep backend scheduling/resource ownership separate from
  `CommandRecorder` implementation; Vulkan and OpenGL own short-lived
  recorders (2026-08-29).
- [x] Keep render-target readback in backend-owned services, exposed as a
  borrowed common interface; Render has no native attachment or synchronization
  access (2026-08-29).
- [x] Preserve the current unlit `ScenePass`, frame-local binding lifetime, and
  Vulkan/OpenGL smoke baseline while the new path is introduced.

**Done when:** deferred-PBR work can add pass policy without restoring the
deprecated OpenGL global/backend implementation pattern.

## D1 — light source, snapshot, and shadow ABI

**Goal:** establish the Gameplay-to-Render light boundary, then the extensible
render-owned light/shadow ABI, before choosing any backend target layout.

### D1.1 — Gameplay light source publication

- [x] Add Gameplay `LightActor`/light-component source state with
  create/update/destroy commands carrying only authored light values. It must
  follow the mesh-proxy publication boundary and expose no resolved
  `LightHandle`, target, descriptor, or native API type. Gameplay may retain
  only an opaque source-registration token to submit later updates/destruction.

**Done when:** Gameplay can own a light’s authoring identity and lifetime while
publishing only source data to Runtime/Render. **Landed 2026-08-29:**
`DirectionalLightComponent` and `CreateDirectionalLightActor` publish
coalesced directional source values through `ILightSourceSink`; Render-side
inbox/snapshot resolution remains D1.2.

### D1.2 — Render light snapshot resolution

- [x] At the RenderSystem frame boundary, resolve source commands into private
  generational `LightHandle` values and immutable render-owned `LightWorld`
  snapshots. Gameplay must never retain those handles.
- [x] Add contract tests for source create/update/destroy ordering,
  stale/forged render handles, and snapshot immutability.

**Done when:** every recorded pass reads one immutable, render-owned light
snapshot; it never reads a Gameplay component directly. **Landed 2026-08-29:**
`LightSourceRegistry` serializes Game-thread source commands into
`LightWorld` at `RenderSystem::BeginFrame`. The registry alone maps the
Gameplay source token to a private `LightHandle`; `LightWorld::Snapshot`
returns copied directional-light values. Point/spot records and the
`LightType` ABI remain D1.3.

### D1.3 — extensible light and shadow description

- [x] Define `LightType::{Directional, Point, Spot}` with shared
  color/radiance, enable state, layer/mask, and optional `ShadowHandle`.
- [x] Define type-specific payloads: directional direction; point
  position/range; spot position/direction/range/inner and outer cones.
- [x] Define `ShadowHandle` and typed job data with
  `ShadowKind::{Directional2D, Spot2D, PointCube}`, resolution, source light,
  and a render-private binding slot.

**Done when:** Render can express all planned light and shadow kinds without
making a GPU resource part of Gameplay or Asset state. **Landed 2026-08-29:**
`LightDesc` now validates matching directional/point/spot payloads, common
light data, and optional shadow identity. `ShadowJobDesc` validates its source
identity/resolution, while `IsShadowKindCompatible` prevents a later scheduler
from pairing an incompatible light type and shadow representation. D4 still
owns job-registry scheduling and target allocation.

### D1.4 — frame lighting binding ABI

- [x] Upload a versioned, bounded `LightGpuData` array and count through
  per-frame bindings. The shader ABI switches on all three light types; an
  unavailable shadow is explicitly unshadowed.
- [x] Add contract tests for type payload validation, layout/version
  compatibility, bounded-count handling, and unshadowed fallback.

**Done when:** the renderer can carry directional, point, and spot data without
Gameplay or Asset knowing GPU bindings, and initial content enables one
directional light only. **Landed 2026-08-29:** Render now packs the immutable
snapshot into a 64-record, version-1 uniform payload at set 0/binding 4 for
the active frame. The current unlit pipeline does not declare this binding, so
the payload is uploaded but intentionally not attached to that descriptor set;
D5 attaches the same `FrameLightingBinding` to the deferred-lighting pipeline.
All unresolved shadow identities encode as `Unshadowed` until D4 supplies a
current-frame shadow binding. Runtime composes the default directional
`LightActor` beside the bootstrap static-mesh actor, so the first Engine scene
publishes exactly one directional source.

## D2 — attachment-capable common graphics contract

**Goal:** support exactly the depth, G-buffer, HDR, and final-color targets
the scheduled passes require.

- [x] Extend common target/pipeline descriptions for multiple named color
  attachments, optional depth, depth-only targets, sampled attachments, and
  pipeline-format compatibility.
- [x] Make Vulkan and OpenGL translate these descriptions without exposing
  native handles in common contracts.
- [x] Add render-private `RendererFrameTargets` that rebuilds the named target
  set for one extent/format policy and never becomes a graph allocator.
- [x] Preserve resize, frame-slot retirement, capture, and shutdown safety for
  every replaced attachment generation.
- [x] Add cross-backend contract/smoke coverage for depth-only, multiple
  attachments, sampling, resize, and teardown.
- [x] Make the D2 sample policy explicit: only single-sample targets are valid
  until a multisample resolve and sampled-MSAA contract exists (2026-08-29).

**Done when:** Render can request compatible single-sample logical attachments
while Graphics owns allocation, transitions, framebuffers, and fence-safe
retirement. Multisample targets remain an explicit later contract.
**Landed 2026-08-29:** `RenderTargetDesc` now carries
`std::vector<RenderTargetColorAttachment>` + optional
`RenderTargetDepthAttachment` (load/store ops, clear values, opt-in sampled
depth), with `ValidateRenderTargetDesc` / `ValidateRenderTargetPipelineCompatibility`
as the shared gate. `PipelineDesc` semantics relax to: empty color formats =
depth-only (legal), `TEXTURE_FORMAT_UNKNOW` depth = no depth (legal); the
Vulkan auto-fill is gone and both backends store the caller's formats on the
pipeline resource. Vulkan dynamic rendering builds N `VkRenderingAttachmentInfo`
(color/depth/depth-only), and depth test/write is derived from the pipeline's
depth format (a no-depth pipeline must not discard fragments). OpenGL creates
N `GL_COLOR_ATTACHMENT{i}` FBO attachments with `glDrawBuffers` / `glDrawBuffer(GL_NONE)`,
and derives `GL_DEPTH_TEST` the same way. `RenderBackend` exposes
`GetRenderTargetColorAttachment(index)` / `GetRenderTargetDepthAttachment`
(no native handles leak). Render now owns `RendererFrameTargets` (D2.5:
`RenderTargetName::SceneColor` → `render::RenderTarget`, extent/format policy
in `BuildDesc`, `RebuildForExtent` with `WaitIdle`, `Cleanup`); `RenderSystem`
runs through it for init/capture/extent/pass/shutdown. The `GraphicsSmoke`
D2 block proves multi-attachment (RGBA8 + RGBA16F + D32) → sampled readback →
depth-only lifecycle on both APIs. The original D2 fullscreen proof used
`cull_mode = NONE` because Vulkan translated the common winding enum literally.
D5.2.1 corrected Vulkan's positive-viewport winding translation; the shared
triangle now uses ordinary back-face culling on both APIs.

## D3 — Material Asset V2 and PBR G-buffer

**Goal:** make the supplied PBR material data authorable and draw opaque
proxies into a documented G-buffer.

- [x] Extend the versioned material asset schema with `StandardPbr` and
  explicit base-color, normal, metallic, roughness, ambient-occlusion, and
  emissive semantics, including color-space rules.
- [x] Extend render-side material resolution/templates and validate pass
  compatibility for `ShadowDepth` and `GBuffer`.
- [x] Audit mesh tangent-space convention and normalize it at the import or
  shader boundary; do not let individual backend shaders choose conventions.
- [x] Define G-buffer attachment encodings for albedo, normal, material
  parameters, and depth, with documented formats and clear values.
- [x] Implement `GBufferPass` from opaque ready MeshProxy draw lists and bind
  PBR material/frame constants.
- [x] Use the supplied rock and light-gold/rusted-iron textures as the first
  asset acceptance fixture.

**Done when:** an opaque proxy writes a stable, inspectable G-buffer on both
backends using a Material Asset V2 rather than bootstrap hardcoded bindings.
**Landed 2026-08-29.** Material assets now version to 2 (`kMaterialVersion = 2`,
V1 unlit files still load; `standard_pbr` is rejected in V1 and version 3 is
rejected outright). `MaterialAssetResolver` branches on the shading model and
builds the canonical 10-param `StandardPbr` template (base_color vec4@0,
metallic f@16, roughness f@20, occlusion f@24, emissive vec4@32; textures at
bindings 2/5/6/7/8 with binding 4 reserved for D5 frame lighting). Per-texture
color-space intent (`Srgb`/`Linear`) keys the texture cache `{asset_id,
color_space}` — base_color is RGBA8_SRGB, the four linear maps are RGBA8_UNORM
— and the normalization rule forces a scalar to 1.0 when its texture is
authored, and injects the two new 1×1 default PNGs
(`asset/texture/default/default_white.png`, `default_flat_normal.png`) when it
is not. **Tangent audit result:** Assimp's `CalcTangentSpace` produces the
5-attribute layout (position/normal/uv/tangent/bitangent, 56-byte
`data::Vertex`); the G-buffer shader declares that layout as canonical, builds
world TBN via `transpose(inverse(mat3(model)))` with `mat3(T,B,N)` column-major
and normal decode `rgb*2-1`; zero-length tangent inputs are guarded before
normalization, and the fragment stage orthonormalizes the basis before applying
the normal map (no-UV meshes fall back to the geometric normal).

The G-buffer target is 3-color + D32: albedo RGBA8_UNORM (linear, clear
{0,0,0,0}), normal RGBA16F (raw world-space, clear {0,0,1,0}), material
RGBA8_UNORM (metallic R / roughness G / occlusion B, A=1, clear {0,1,1,0}), D32
clear 1.0. `RenderSystem` now schedules `GBufferPass` (opaque draw lists via
`SceneDrawListBuilder::Build(..., MaterialPass::GBuffer)`) → `GBufferDebugViewPass`
(fullscreen three-panel albedo/normal/material inspection into SceneColor) →
`EditorCompositePass`; the unlit `ScenePass` is dropped from the engine schedule
(unlit stays green in the smoke). `MaterialPass::{Scene, ShadowDepth, GBuffer}`
threads through `FindMaterialPipeline(handle, pass)`, `CreateMaterialBinding`,
`RecordMeshProxy`, and the draw-list builder, and the pipeline-cache key folds
the pass. The `GraphicsSmoke` D3 block proves the full path on both APIs —
`rock_pbr.material` → template → GBuffer pipeline → 3-color+depth write →
debug composite → `save/screenshots/validation/graphics-smoke-d3-{vulkan,opengl}.png`
with non-uniform verified pixels (the rock fills ~half the frame). Shadow pass
compatibility (`ShadowDepth`) is reserved for D4.

## D4 — directional shadow pass family

**Goal:** prove the first producer/consumer shadow path without making
directional light a public special case.

- [x] Schedule typed shadow jobs from immutable light/shadow snapshots;
  `ShadowDepthPass` remains a pass family.
- [x] Implement one `Directional2D` depth target, light-space constants, and
  opaque `visible && casts_shadow && ready` caster filtering.
- [x] Bind the scheduled depth result through its render-private shadow slot
  for deferred lighting.
- [x] Define explicit missing/invalid-shadow behavior and safe target rebuild
  on resize/shutdown.
- [x] Add depth/shadow-visibility debug conversion views through Render’s
  capture resolver, never raw depth readback.

**Done when:** one directional light produces and consumes a depth shadow map
in the fixed schedule on Vulkan and OpenGL. **Landed 2026-08-30:** Gameplay
publishes only `casts_shadow`; `LightSourceRegistry` creates/retires the
private `ShadowHandle` while resolving the source. `RenderSystem` selects at
most one enabled `Directional2D` job from the immutable snapshot, uses a
camera-centred 100×100 orthographic fit (200-unit depth range), and records a
depth-only D32 2048² target before `GBufferPass`. It filters
`visible && casts_shadow && opaque && ready` proxies, with a generic depth
override pipeline and frame-local set-0 bindings 0/1. The depth target is
shader-readable only to Render and is sampled by the fourth panel of the
existing SceneColor debug conversion; an absent/invalid job skips recording
and displays the target's clear depth. The current frame's private job carries
binding slot 0 for D5. `GraphicsSmoke` now records, samples, captures, resizes,
and tears down a real D32 sampled depth target on both Vulkan and OpenGL.
Depth bias is deliberately not claimed: the common pipeline state has no
portable bias contract yet, so it remains a D5 prerequisite rather than an
API-specific escape hatch.

## D5 — deferred lighting and presentation

**Goal:** consume G-buffer and typed light data into HDR, then present a
stable final SceneColor.

- [x] Implement fullscreen `DeferredLightingPass` with metallic-roughness PBR
  evaluation and the versioned light type switch.
- [ ] Populate one directional record and directional shadow factor in the
  first visual milestone; point/spot records remain valid unshadowed inputs.
- [x] Write HDR `SceneHdr`, then implement a documented `ToneMapPass` to final
  LDR `SceneColor` before the existing Editor composite bridge.
- [ ] Enable G-buffer, shadow, and final-color capture views by explicit
  Render conversion passes.
- [ ] Capture and inspect deterministic Vulkan/OpenGL PBR screenshots using
  the runtime capture command path.

**Done when:** the shared scene produces comparable PBR final-color captures on
both backends, with debug views sufficient to diagnose G-buffer and shadow
errors.

### D5.1 — HDR presentation spine (landed 2026-08-30)

`RendererFrameTargets` now owns a sampled RGBA16F `SceneHdr` beside the stable
RGBA8 sRGB `SceneColor`. The fixed schedule validates
`GBufferDebugViewPass -> SceneHdr -> ToneMapPass -> SceneColor ->
EditorCompositePass`; the diagnostic conversion temporarily occupies the HDR
producer slot until `DeferredLightingPass` replaces it. `ToneMapPass` applies
global Reinhard in linear space and relies on the SceneColor attachment for
the final sRGB transfer. Target/resource counts derive from enum sentinels,
which also fixes D4's stale manual target count that made
`DirectionalShadow` inaccessible through `RendererFrameTargets`.

`GraphicsSmoke` proves G-buffer + sampled shadow -> RGBA16F -> tone map ->
captured PNG on Vulkan and OpenGL at
`save/screenshots/validation/graphics-smoke-d5-{vulkan,opengl}.png`. This slice
does **not** claim deferred PBR lighting, shadow-factor evaluation, HDR capture,
or runtime command-path capture; those remain the next D5 subtasks.

### D5.2 — directional Cook-Torrance lighting (landed 2026-08-30)

The fixed schedule now runs `DeferredLightingPass` as the sole `SceneHdr`
writer. Its fullscreen shader samples linear albedo, raw world normal,
metallic/roughness/occlusion, and shader-readable G-buffer D32; reconstructs
world position with the inverse view-projection matrix and the backend's depth
range; and evaluates GGX/Smith/Schlick Cook-Torrance lighting for enabled
directional records. The version-1 `LightGpuData` std140 layout is locked with
exact CPU size/offset assertions. Point and spot records are valid but ignored
until D6, and shadow state remains deliberately unconsumed until D5.3.

`GraphicsSmoke` now drives that real light UBO + depth-reconstruction path into
RGBA16F, tone maps it, and captures the supplied rock on Vulkan and OpenGL.
Both backends pass the automated non-uniform/direct-light threshold. D5.2.1
then corrected Vulkan's winding translation: normal-debug captures became
pixel-identical except for one 1/255 channel value, and final lighting has the
same bound and mean on both APIs. This is now a valid baseline for D5.3.
Runtime command-path debug capture remains separate work.

## D6 — light and shadow expansion

**Goal:** use the ABI without a second material, proxy, or common-RHI redesign.

- [ ] Enable unshadowed point and spot lighting using the existing
  `LightGpuData` type switch.
- [ ] Add `Spot2D` shadow jobs, then `PointCube` jobs only after their
  target/binding lifetime and performance budgets are documented.
- [ ] Add shadow target allocation/budget policy and tests for disabled,
  stale, unsupported, and over-budget jobs.
- [ ] Evaluate cascades, atlases, IBL, clustered/forward+ selection, and a
  render graph only from measured light/pass dependency pressure.

**Done when:** new light types extend render policy and shaders while Asset,
Gameplay, MeshProxy, and the common recorder contract remain stable.

## D7 — evidence and handoff

- [ ] Add unit/contract coverage for light/shadow handles, material schema,
  target compatibility, and pass filtering.
- [ ] Run the validation-matrix targets for changed Render and Graphics code;
  do not run concurrent CMake/MSBuild builds.
- [ ] Run Vulkan and OpenGL `GraphicsSmoke`, including resize, capture, and
  teardown, then inspect generated PBR screenshots.
- [ ] Update [status](../status.md), the design plan, and this ledger with
  landing dates, exact commands, results, and known visual limitations.
- [ ] Revisit the fixed schedule only if evidence shows real dependency,
  aliasing, or scheduling pressure.

## Explicitly deferred

- Alpha-mask/blended deferred geometry, decals, terrain, hair, and material
  graph generation.
- Cascaded directional shadows, shadow atlases, cube-map shadow optimization,
  IBL, clustered/forward+ lighting, and graph-driven aliasing.
- Any revival of deprecated OpenGL renderer ownership or native API types in
  common Render/Graphics contracts.
