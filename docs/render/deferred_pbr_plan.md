# Deferred PBR Renderer Plan

**Status:** proposed (2026-08-29)  
**Parent roadmap:** [Mesh Proxy After MP3](../world/mesh_proxy_TODO.md#after-mp3)  
**Execution spec:** [render-deferred-pbr](../../.spec/specs/render-deferred-pbr.md)
**Execution ledger:** [Deferred PBR TODO](deferred_pbr_TODO.md)

## Objective

Replace the current single-target unlit `ScenePass` with the first real proxy-derived renderer:

```text
DirectionalShadow -> GBuffer -> DeferredLighting -> ToneMap/EditorComposite
```

It must use the current Asset -> Resource -> Render -> Graphics boundary on both Vulkan and OpenGL. The deprecated OpenGL renderer is evidence for shader and scene requirements only; none of its GL object ownership, global access, or proxy draw API may return.

The first deliverable is deliberately small in execution: one directional light with one 2D depth map, opaque static mesh proxies, metallic-roughness PBR, and a fullscreen deferred-lighting pass. Its light/shadow ABI is deliberately extensible to directional, point, and spot lights from the beginning; only the directional producer is enabled in the first visual milestone.

## Local starting point

- `RenderSystem` owns the backend, `RenderWorld`, `MaterialSystem`, resource resolver, `FrameContext`s, and ordered `RenderPassSchedule`. Its `ScenePass` currently draws proxies into one `RenderTarget`.
- `MeshProxy` already has visibility, bounds, transform, material identity, opaque classification, and `casts_shadow`; `RenderWorld` owns proxy storage and `SceneDrawListBuilder` batches opaque work.
- `FrameContext` owns transient uniforms/descriptors until the backend frame slot is reusable. `RenderResourceResolver` owns static mesh, texture, sampler, and pipeline handles.
- Common Graphics supports one offscreen `RenderTargetHandle` and a pipeline description with color/depth formats. It does not describe named attachments, depth-only targets, load/store, or a sampled target attachment.
- Material Asset V1 accepts only `unlit`; the resolver accepts only `base_color_texture`. Render-side `StandardPbr` exists but cannot yet be authored.
- Supplied validation assets are `asset/texture/pbr/light-gold-bl/`, `asset/texture/pbr/rustediron1-alt2-bl/`, and `asset/model/rock1-bl/`. The rock asset contains albedo, AO, metallic, OpenGL tangent-space normal, roughness, and mesh.

## Design decision

**Question:** How can shadow, G-buffer, and PBR be introduced without turning `RenderSystem` into backend code or prematurely committing to a generic graph?

**Decision:** expand the ordered pass declarations into typed render-owned inputs and outputs, then add the smallest common RHI attachment contract required by those concrete passes. V1 retains an explicit fixed schedule and Render-owned `RendererFrameTargets`. A graph remains deferred until actual passes prove dependency, aliasing, or scheduling pressure.

| Pass | Reads | Writes | Draw source | First scope |
|---|---|---|---|---|
| `ShadowDepthPass` | one scheduled shadow job's light/camera constants | a typed shadow target | visible opaque `casts_shadow` proxies | dispatches one directional 2D job first; point/spot jobs are reserved |
| `GBufferPass` | material, camera | albedo, normal, material, depth | visible opaque proxies | no blend/alpha mask |
| `DeferredLightingPass` | G-buffer + typed shadow bindings | HDR `SceneHdr` | fullscreen triangle | generic light record; one directional light populated first |
| `ToneMapPass` | `SceneHdr` | final `SceneColor` | fullscreen triangle | one documented tone map |
| `EditorCompositePass` | final `SceneColor` | presentation bridge | Editor-owned | existing bridge |

`SceneColor` remains the capture service’s final stable output. Internal HDR and LDR targets must not be conflated.

## Ownership and lifetime

```text
Gameplay LightActor/components + logical material/mesh IDs
  -> Runtime publishes value-only light source updates at frame boundary
  -> RenderSystem resolves sources into private LightWorld handles
  -> immutable RenderWorld + LightWorld snapshots
  -> pass builders select draw lists and PipelineDesc
  -> RendererFrameTargets owns named logical target set/generation
  -> Graphics owns GPU allocation, transitions, submission, retirement
```

- Gameplay owns LightActor/component identity, authoring values, and lifetime. It
  publishes a value-only light source description; it does not store a
  `LightHandle`, shadow target, descriptor, or backend type.
- Asset owns file identity, decode, and CPU material data; it never owns a GPU descriptor or target.
- Render owns the private `LightWorld` snapshot/handles, light selection,
  pass policy, PBR semantics, draw classification, and logical target-set
  lifetime.
- `LightSourceRegistry` is the only source-token-to-`LightHandle` map. It
  accepts copied Gameplay commands under its inbox lock, then drains and applies
  them at `RenderSystem::BeginFrame`; only `LightWorld::Snapshot` values
  may reach passes.
- Graphics owns attachment allocation, layout/state, recording/submission, and fence-safe destruction. Common interfaces expose no `Vk*` or `gl*` values.
- Rebuild targets only before a frame. Graphics retires replacements after their last submission completes; Render does not retain stale attachments across generations.
- Passes consume immutable snapshots and do not call Asset, Gameplay components, or raw backend APIs while recording.

## Required contracts

### Render

1. Add Gameplay `LightActor`/light-component source state that publishes
   create/update/destroy value descriptions through the Runtime-to-Render
   boundary, matching the mesh-proxy flow. Render resolves those source
   descriptions at the frame boundary into a private `LightWorld` snapshot
   with stable generational light identity and `LightType::{Directional,Point,Spot}`.
   Each render record has common radiance/color, enabled, layer/mask, optional
   shadow identity, and type-specific data: direction for directional,
   position/range for point, and position/direction/range/cones for spot. The
   first scene publishes one directional source only.
2. Add `MaterialPass::{ShadowDepth,GBuffer,DeferredLighting,ToneMap}`. Surface templates declare compatible geometry passes; shadow uses a depth override pipeline, not a surface fragment shader.
3. Add a render-owned `ShadowHandle`/job registry. A shadow job records its light identity, `ShadowKind::{Directional2D,Spot2D,PointCube}`, target resolution, and a render-private binding slot. A missing/invalid shadow handle means unshadowed lighting; it is not encoded as a texture handle in Gameplay or Asset.
4. Upload a versioned `LightGpuData` array plus light count through the per-frame lighting binding. The initial shader switch handles all three type values and treats unavailable shadow kinds as unshadowed. This preserves ABI compatibility when point/spot lights are enabled without requiring a material-ABI rewrite.
5. Add pass-specific lists. Shadow filters `visible && casts_shadow && opaque && ready`; G-buffer filters `visible && opaque && ready`.
6. Add `RendererFrameTargets`, a Render-private owner that rebuilds named targets for one extent/format policy. It is not a graph allocator.

### Extensible light and shadow contract

`ShadowDepthPass` is a pass family, not a directional-only public concept. Render schedules zero or more typed jobs from the immutable light/shadow snapshot; Graphics receives only the selected target and pipeline-compatible draw commands. This prevents later point/spot shadows from changing Gameplay, Asset, `MeshProxy`, or the common recorder contract.

| Light type | Deferred data | Initial shadow representation | First implementation state |
|---|---|---|---|
| Directional | normalized direction, radiance | `Directional2D` depth target and light-space matrix | enabled |
| Point | position, range, radiance | `PointCube` depth target, six view-projection matrices, far plane | ABI reserved; unshadowed lighting next |
| Spot | position, direction, range, inner/outer cone, radiance | `Spot2D` depth target and light-space matrix | ABI reserved; unshadowed lighting next |

The first lighting buffer has a documented bounded count compatible with the current uniform-binding path. If profiling/content requires more lights, introduce a common storage-buffer binding as a separately validated RHI extension; do not bake a hidden backend-specific maximum into shaders.

### Graphics

1. Replace the implicit color-plus-depth target assumption with validated attachments: color array, optional depth, extent, samples, load/store/clear policy, and shader-read usage. Depth-only is legal.
2. Add an opaque sampled attachment view/binding. Graphics maps it to image/view/texture and tracks API layout/state privately.
3. Keep recording pass-scoped: begin compatible target, bind compatible pipeline, draw, end. Use the common mesh/draw model for a fullscreen triangle; no raw `glDrawArrays` escape hatch.
4. Pipeline validation must match G-buffer formats and depth-only targets on both backends before allocation where practical.

Do not expose public barriers, framebuffers, image layouts, or native texture objects. Add a common transition/usage concept only if a concrete cross-pass dependency cannot be represented by attachment intent.

### Material and shader ABI

Material Asset V2 adds `standard_pbr` and a fixed first semantic set:

| Semantic | Type | Texture color space | Default |
|---|---|---|---|
| `base_color` / texture | vec4 / 2D | sRGB -> linear | white |
| `normal_texture` | 2D | linear | flat tangent normal |
| `metallic` / texture | float / 2D | linear | 0 |
| `roughness` / texture | float / 2D | linear | 1 |
| `occlusion` / texture | float / 2D | linear | 1 |

The G-buffer stores a declared normal space, linear base color, metallic, roughness, AO, and reconstructible depth. Do not store world position merely because the legacy shader did; reconstruct from depth unless profiling shows a first-slice need. Parser validation enforces semantic names, types, numeric ranges, and color-space policy; Render maps semantics to explicit bindings.

**Landed G-buffer encodings (2026-08-29):** 3 color + depth, single-sample.

| Attachment | Format | Content | Clear |
|---|---|---|---|
| 0 albedo | RGBA8_UNORM | linear base color (hardware sRGB-decode already linearized) | {0,0,0,0} |
| 1 normal | RGBA16F | raw world-space normal, [-1,1] (no `*2-1` encode) | {0,0,1,0} |
| 2 material | RGBA8_UNORM | metallic R / roughness G / occlusion B, A=1 | {0,1,1,0} |
| depth | D32 | device depth | 1.0 |

Emissive is a **constant only** (not a G-buffer channel) until D5 decides how to consume it.

**Landed color-space rule (2026-08-29):** per-texture intent — `base_color` = Srgb (RGBA8_SRGB), normal/metallic/roughness/occlusion = Linear (RGBA8_UNORM). The texture cache key is the typed pair `{AssetID, MaterialTextureColorSpace}` so one asset sampled in both spaces gets two GPU textures without overlapping AssetID generation bits. Normalization is scalar-explicit, not shader-flagged: authored texture → its scalar forced to 1.0; scalar-only → identity default asset (`default_white.png`, `default_flat_normal.png`); neither → plan-table default. The shader always samples all five textures × scalars.

**Landed tangent convention (2026-08-29):** canonical 5-attribute layout — position@0, normal@1, texcoord@2, tangent@3, bitangent@4 (56-byte `data::Vertex`, Assimp `CalcTangentSpace`). World TBN = `transpose(inverse(mat3(model)))`, `mat3(T,B,N)` column-major, normal decode `rgb*2-1`; fragments with degenerate tangents (zero-filled for no-UV meshes) fall back to the geometric normal.

The old `defer_pbr.frag` is a formula reference, not an ABI: it relies on loose uniforms, GL binding state, hard-coded counts, and an uninitialized `direct_color`. New shaders use one versioned UBO/descriptor layout shared by Vulkan and OpenGL. The `StandardPbr` constant-block ABI (canonical 10-param order) is `base_color` vec4@0, `metallic` float@16, `roughness` float@20, `occlusion` float@24, `emissive` vec4@32, with samplers at bindings 2/5/6/7/8 and binding 4 reserved for the D5 frame-lighting block.

## Delivery stages

1. **Recorder ownership prerequisite (complete, 2026-08-29):** [Vulkan and OpenGL now both vend a short-lived recorder](../graphics/command_recording_ownership_plan.md); neither backend scheduler implements `CommandRecorder`.
2. **Contract baseline:** document target requirements; add invalid-target and pipeline tests; preserve unlit scene/capture smoke.
3. **Attachment RHI:** implement multi-attachment/depth-only creation, sampled attachments, resize recreation, compatibility validation, and safe retirement on both backends.
4. **Material V2 + G-buffer (landed 2026-08-29):** extend parsing/resolution, add shaders/pipelines, and render opaque supplied PBR content through `GBufferPass`. Material assets version to 2; `GBufferPass` + `GBufferDebugViewPass` replace the unlit `ScenePass` in the engine schedule; the smoke D3 block proves `rock_pbr.material` → GBuffer → composite on Vulkan and OpenGL.
5. **Light ABI + directional shadow:** add the generic light/shadow snapshot and shader ABI, then enable one directional depth job with stable camera-derived fit, bias policy, and binding in lighting.
6. **Lighting + tone map:** implement the type-switching linear HDR Cook-Torrance pass, initially with one directional light; tone map once to final `SceneColor`. IBL stays disabled until it has owned derived-environment assets.
7. **Light expansion:** enable non-shadowed point and spot records first, then add `Spot2D` and `PointCube` shadow jobs, target allocation/budget policy, and the related debug views. Cascades and atlasing need measured content pressure and remain separate work.
8. **Evidence/hardening:** debug views, per-pass timing, resize/shutdown stress, scenario screenshots, status/docs/journal updates.
9. **Graph gate:** decide on a graph only from demonstrated target lifetime, aliasing, or scheduling needs.

## Non-goals

- No generic graph, transient aliasing, async compute, or GPU-driven redesign.
- No point/spot/cascaded shadow **implementation**, transparency/alpha masks, SSAO/SSR/bloom, clustered lighting, or IBL preprocessing in the first renderer. Point/spot light and shadow identities remain part of the first light/shadow ABI.
- No reuse of deprecated `ShadowManager`/caster classes or direct OpenGL code.
- No Asset-to-GPU shortcut or backend-specific material path.

## Reference findings

| Reference | Observed evidence | Local conclusion |
|---|---|---|
| [gkNextEngine ShadowMapPass](https://github.com/gameknife/gkNextEngine/blob/main/src/Engine/Rendering/Shadow/ShadowMapPass.cpp) | It creates a depth-only shadow pass then makes depth shader-readable; it owns four Vulkan cascade framebuffers. | Adopt explicit depth producer/consumer intent and depth-only validation. Reject native Vulkan objects, global bindless descriptors, indirect GPU draws, and cascades for V1. |
| [gkNextEngine FrameSubmission](https://github.com/gameknife/gkNextEngine/blob/main/src/Engine/Rendering/FrameSubmission.cpp) | It waits frame-slot fences before reuse and advances completed submission serials. | Target replacement uses existing frame-slot/fence-safe retirement, never a Render-side immediate delete. |
| [Godot deferred pipeline](https://github.com/godotengine/godot/blob/master/servers/rendering/renderer_rd/pipeline_deferred_rd.h) | Deferred pipeline configuration is separate from scene policy. | Keep G-buffer pipeline/pass configuration in Render, not Gameplay or Graphics backends. Its broader renderer is not imported. |
| [Godot RenderSceneBuffers](https://github.com/godotengine/godot/blob/master/servers/rendering/storage/render_scene_buffers.h) | Renderer-owned scene-buffer configuration is separate from scene-facing code and is configured behind the rendering system. | The supporting D1.2 precedent: keep resolved per-scene light state in Render. Reject Godot's ref-counted server hierarchy; KimPeanut needs only a mutex-protected source inbox and value snapshots. |
| [Filament LightDefinition](https://github.com/google/filament/blob/main/libs/viewer/include/viewer/Settings.h) | One typed value carries shared color/intensity with position, direction, falloff, cone, and shadow intent for the engine's light-manager types. | Adopt the typed value shape: `LightDesc` has shared state plus a type-matched directional/point/spot payload. Reject its engine manager/entity and shadow-options model; KimPeanut keeps handles/jobs Render-private and defers target policy to D4. |
| [Filament scene light preparation](https://github.com/google/filament/blob/main/filament/src/details/Scene.cpp) | The scene gathers immutable light data, bounds the prepared list, then serializes positional records into a GPU buffer with an explicit per-record structure; shadow data is supplied only when its renderer state is available. | D1.4 adopts one bounded, versioned POD payload at the Render frame boundary. Unlike Filament’s SoA and driver buffer path, KimPeanut uses the current common UBO allocator and does not serialize a `ShadowHandle` as a GPU resource: unresolved shadows are explicitly `Unshadowed` until D4. |
| [bgfx IBL example](https://github.com/bkaradzic/bgfx/blob/master/examples/18-ibl/ibl.cpp) | Environment resources and PBR/lighting values flow through API-neutral handles. | Use only as cross-API PBR boundary evidence; defer IBL until KimPeanut owns environment-derived assets. |

The local [Sakura study](../graphics/sakura_reference.md) remains evidence for a later graph when concrete aliasing/lifetime pressure exists. Historical KimPeanut code at `c19776f^` directly owned GL FBOs, textures, and proxy draws; that boundary is explicitly rejected.

## Acceptance criteria

- [ ] Both backends deterministically reject malformed targets and incompatible pipelines.
- [ ] Vulkan and OpenGL render supplied metallic-roughness content through the same RenderWorld/proxy path without native types leaking upward.
- [ ] A captured SceneColor visibly contains one directional shadow and target retirement is safe across resize/shutdown; the same lighting ABI accepts unshadowed point/spot records without changing material or common RHI contracts.
- [ ] Render-owned debug capture can expose shadow, every G-buffer attachment, HDR lighting, and final SceneColor.
- [ ] Texture sRGB/linear policy and supplied OpenGL normal convention are validated; tangent availability is audited before normal maps are enabled.
- [ ] Unlit bootstrap and current cross-backend smoke stay green until an intentional PBR-scene replacement.
- [ ] The graph decision is recorded from evidence rather than a feature checklist.

## Validation

Stages 1–2 require Level 2 `GraphicsContractTest` and Level 3 `GraphicsSmoke` on Vulkan and OpenGL. Runtime stages also require `RenderPassScheduleTest`, material-loader/resolver tests, post-resize screenshot evidence, and debug-view captures. Use `tools/kp.ps1` for targeted builds; never run concurrent CMake/MSBuild builds.

## Risks and open questions

- Exact attachment formats and sample policy require a current RHI/backend audit.
- Assimp tangent availability needs an explicit contract; ship without normal mapping first if it cannot be guaranteed.
- Shadow fitting, depth bias, and reverse-Z convention require a camera/math audit; the legacy fixed orthographic box is not acceptable.
- Deferred MSAA and transparent geometry remain separate design decisions.
- Document PBR asset licensing/import metadata and normal-map convention when material assets are added.
