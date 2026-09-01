# Deferred PBR Renderer Roadmap

**Status: active.** This page is the roadmap and acceptance ledger for the
[Deferred PBR Renderer Plans](PLANS.md) and its
[implementation spec](../../../.spec/specs/render-deferred-pbr.md). Detailed
implementation history, validation evidence, corrections, and remaining risks
belong in the [execution journal](../../../.spec/journal/render-deferred-pbr.md).
The renderer uses a fixed ordered-pass schedule; this is not a render-graph
proposal.

## D0 — completed prerequisites

- [x] Keep backend scheduling/resource ownership separate from
  `CommandRecorder` implementation; Vulkan and OpenGL own short-lived
  recorders.
- [x] Keep render-target readback in backend-owned services behind a borrowed
  common interface; Render has no native attachment or synchronization access.
- [x] Preserve the unlit `ScenePass`, frame-local binding lifetime, and the
  Vulkan/OpenGL smoke baseline while the new path is introduced.

**Done when:** deferred-PBR work can add pass policy without restoring the
deprecated OpenGL global/backend implementation pattern. See the journal's
[D0–D3 foundation entry](../../../.spec/journal/render-deferred-pbr.md#2026-08-29--d0d3-foundation).

## D1 — light source, snapshot, and shadow ABI

**Goal:** establish the Gameplay-to-Render light boundary and the extensible
render-owned light/shadow ABI before choosing backend target layouts.

### D1.1 — Gameplay light source publication

- [x] Publish authored light values through create/update/destroy source
  commands. Gameplay retains only an opaque source-registration token and no
  resolved handle, target, descriptor, or native API type.

### D1.2 — Render light snapshot resolution

- [x] Resolve source commands at the `RenderSystem` frame boundary into private
  generational `LightHandle` values and immutable `LightWorld` snapshots.
- [x] Test source ordering, stale/forged handles, and snapshot immutability.

### D1.3 — extensible light and shadow description

- [x] Define directional, point, and spot light payloads with shared radiance,
  enable, layer/mask, and optional shadow identity.
- [x] Define typed `ShadowJobDesc` values for `Directional2D`, `Spot2D`, and
  `PointCube`, including source identity, resolution, and private binding slot.

### D1.4 — frame lighting binding ABI

- [x] Upload a versioned, bounded `LightGpuData` array and count through
  per-frame bindings, with explicit unshadowed fallback.
- [x] Test payload validation, ABI compatibility, bounded counts, and fallback.

**Done when:** recorded passes consume one immutable render-owned light
snapshot, and planned light/shadow kinds are expressible without exposing GPU
resources to Gameplay or Asset. See the [D0–D3 journal entry](../../../.spec/journal/render-deferred-pbr.md#2026-08-29--d0d3-foundation).

## D2 — attachment-capable common graphics contract

**Goal:** support exactly the depth, G-buffer, HDR, and final-color targets
required by the scheduled passes.

- [x] Support multiple named color attachments, optional depth, depth-only
  targets, sampled attachments, and pipeline-format compatibility.
- [x] Translate the contract in Vulkan and OpenGL without native types in
  common Render/Graphics APIs.
- [x] Keep `RendererFrameTargets` render-private and extent/format driven; it
  is not a graph allocator.
- [x] Preserve resize, frame-slot retirement, capture, and shutdown safety for
  replaced attachment generations.
- [x] Cover multiple attachments, sampling, depth-only targets, resize, and
  teardown on both backends.
- [x] Keep multisample targets out of scope until sampled-MSAA resolve
  semantics exist; the current policy is single-sample only.

**Done when:** Render requests compatible logical attachments while Graphics
owns allocation, transitions, framebuffers, and fence-safe retirement. See the
[D0–D3 journal entry](../../../.spec/journal/render-deferred-pbr.md#2026-08-29--d0d3-foundation).

## D3 — Material Asset V2 and PBR G-buffer

**Goal:** author supplied PBR material data and draw opaque proxies into a
documented, inspectable G-buffer.

- [x] Add versioned `StandardPbr` material data with explicit texture
  semantics and color-space rules.
- [x] Resolve material templates and validate `ShadowDepth`/`GBuffer` pass
  compatibility.
- [x] Establish one tangent-space convention at the import/shader boundary.
- [x] Document albedo, normal, material-parameter, and depth encodings.
- [x] Implement `GBufferPass` from opaque ready `MeshProxy` draw lists.
- [x] Use supplied rock and light-gold/rusted-iron textures as acceptance
  fixtures.

**Done when:** an opaque proxy writes the stable G-buffer on both backends from
a Material Asset V2 rather than bootstrap-only bindings. See the
[D0–D3 journal entry](../../../.spec/journal/render-deferred-pbr.md#2026-08-29--d0d3-foundation).

## D4 — directional shadow pass family

**Goal:** prove the first producer/consumer shadow path without making a
directional light a public special case.

- [x] Schedule typed shadow jobs from immutable light/shadow snapshots.
- [x] Implement one `Directional2D` depth target, light-space constants, and
  opaque caster filtering.
- [x] Bind the scheduled depth result through its render-private shadow slot.
- [x] Define missing/invalid-shadow behavior and safe resize/shutdown rebuild.
- [x] Add depth and shadow-visibility debug conversion views through Render's
  capture resolver.

**Done when:** one directional light produces and consumes a depth shadow map
on Vulkan and OpenGL. See the
[D4 journal entry](../../../.spec/journal/render-deferred-pbr.md#2026-08-30--d4-directional-shadow-depth-family).

## D5 — deferred lighting and presentation

**Goal:** consume the G-buffer and typed light data into HDR, then present a
stable final `SceneColor` with diagnostic evidence.

- [x] Implement fullscreen metallic-roughness deferred lighting and the
  versioned light type switch.
- [x] Populate directional lighting and shadow visibility in the first visual
  milestone; point/spot records remain valid unshadowed inputs.
- [x] Write HDR `SceneHdr`, tone-map into LDR `SceneColor`, and preserve the
  existing editor composite bridge.
- [x] Route G-buffer, shadow, and final-color capture views through explicit
  Render conversion passes.
- [x] Capture and inspect deterministic Vulkan/OpenGL PBR screenshots through
  the runtime command path.
- [x] Derive environment IBL artifacts through Asset → Resource → Render.

### D5.1 — HDR presentation spine (landed 2026-08-30)

- [x] Separate sampled HDR `SceneHdr` from stable LDR `SceneColor` and add the
  fixed tone-map stage. See the [journal](../../../.spec/journal/render-deferred-pbr.md#2026-08-30--d51-hdr-presentation-spine).

### D5.2 — directional Cook-Torrance lighting (landed 2026-08-30)

- [x] Replace the diagnostic HDR producer with deferred PBR lighting and lock
  the light/deferred-camera ABI. See the [journal](../../../.spec/journal/render-deferred-pbr.md#2026-08-30--d52-directional-cook-torrance-lighting).

### D5.2.1 — cross-backend winding correction (landed 2026-08-30)

- [x] Restore ordinary back-face culling and put framebuffer-origin/viewport
  normalization at the owning backend boundaries. See the [journal](../../../.spec/journal/render-deferred-pbr.md#2026-08-30--d521-cross-backend-winding-correction).

### D5.2.2 — projection/viewport ownership correction (landed 2026-08-30)

- [x] Keep shared projection math y-up, use Vulkan's negative-height viewport,
  and normalize OpenGL capture rows. See the [journal](../../../.spec/journal/render-deferred-pbr.md#2026-08-30--d522-projectionviewport-ownership-correction).

### D5.3 — directional shadow consumption (landed 2026-08-30)

- [x] Consume the scheduled directional depth map with frame-local transforms,
  receiver bias, and 3×3 PCF. See the [journal](../../../.spec/journal/render-deferred-pbr.md#2026-08-30--d53-directional-shadow-consumption).

### D5.4 — runtime diagnostic capture routing (landed 2026-08-31)

- [x] Route six semantic views through Render-owned conversion output and the
  runtime capture command. See the [journal](../../../.spec/journal/render-deferred-pbr.md#2026-08-31--d54-runtime-diagnostic-capture-routing).

### D5.5 — HDR environment background (landed 2026-08-31)

- [x] Load the HDR panorama through Asset and render visible environment
  radiance separately from material IBL. See the [journal](../../../.spec/journal/render-deferred-pbr.md#2026-08-31--d55-hdr-environment-background).

### D5.6 — OpenGL depth-clear state isolation (landed 2026-08-31)

- [x] Restore depth writes before depth Clear load-ops and lock the stale-state
  transition in smoke coverage. See the [journal](../../../.spec/journal/render-deferred-pbr.md#2026-08-31--d56-opengl-depth-clear-state-isolation).

### D5.7 — environment IBL (landed 2026-08-31)

- [x] Process irradiance, roughness-filtered radiance, and BRDF-LUT artifacts
  in Resource; Render owns their bindings and the shader applies split-sum
  IBL. See the [journal](../../../.spec/journal/render-deferred-pbr.md#2026-08-31--d57-environment-ibl).

**Done when:** the shared scene produces comparable PBR final-color captures
on both backends, with debug views sufficient to diagnose G-buffer and shadow
errors. The remaining evidence handoff is tracked in D7.

## D6 — light and shadow expansion

**Goal:** extend the ABI without a second material, proxy, or common-RHI
redesign.

- [x] **D6.1 (2026-08-31):** Enable unshadowed point lighting with bounded
  inverse-square attenuation and range cutoff.
- [x] **D6.2 (2026-08-31):** Enable unshadowed spot lighting with point-like
  attenuation and squared cosine interpolation between cone limits.
- [x] **D6.3.1 (2026-09-01):** Add authored spot `casts_shadow` intent and select at most one
  valid `Spot2D` job deterministically; disabled, stale, incompatible, and
  over-budget lights remain explicitly unshadowed.
- [x] **D6.3.2 (2026-09-01):** Add one Render-owned fixed 1024² sampled D32 spot target,
  perspective light-frustum fitting/culling, and a generalized depth-only
  shadow recorder shared with the directional job.
- [x] **D6.3.3 (2026-09-01):** Resolve directional slot 0 and spot slot 1 together without
  changing the `LightGpuData` stride; consume the spot map with receiver bias
  and 3×3 PCF.
- [x] **D6.3.4 (2026-09-01):** Add `SpotShadowDepth`/`SpotShadowVisibility`
  diagnostics, cover their Render/Runtime routing, and inspect deterministic
  Vulkan/OpenGL runtime captures. Visibility captures contain stable
  object-shaped occlusion; perspective D32 depth is near-white after 8-bit
  conversion for this fixture. Focused source checks pass; rebuilding test
  binaries remains blocked by the host's denied Windows SDK probe. See the
  [D6.3.4 journal entry](../../../.spec/journal/render-deferred-pbr.md#2026-09-01--d634-spot-shadow-diagnostics-and-runtime-proof).
- [x] **D6.4.1 (2026-09-01):** Add point `casts_shadow` authoring, private shadow-handle
  lifetime, and deterministic selection of at most one `PointCube` at slot 2.
- [x] **D6.4.2 (2026-09-01):** Produce six canonical 90-degree point views in one fixed
  3×2 sampled D32 atlas, with range and per-face caster culling.
- [x] **D6.4.3 (2026-09-01):** Consume the point atlas through a separate point-shadow
  constant block, dominant-axis face selection, receiver bias, and tile-clamped
  3×3 PCF without changing `LightGpuData`.
- [x] **D6.4.4 (2026-09-01):** Add `PointShadowDepth`/`PointShadowVisibility`
  diagnostics and inspect the multi-face Vulkan/OpenGL runtime fixtures. The
  point-only validation captures show stable non-uniform visibility on both
  backends; perspective D32 depth is near-white after RGBA8 conversion.
- [x] **D6.4 profile baseline (2026-09-01):** Keep six 512² faces in the fixed
  1536×1024 D32 target (6,291,456 bytes / 6 MiB), retain explicit empty-face
  culling, and add a one-shot Render diagnostic for per-face/total draw counts
  and CPU recording time. GPU timing remains unavailable until timestamp-query
  support exists; a longer warm-runtime session is still needed for numeric
  per-face/CPU samples.
- [ ] Evaluate cascades, atlases, clustered/forward+, and a render graph only
  from measured light/pass dependency pressure.

See the [D6 stage design](.plan/D6.md), the
[D6.4 point-shadow design](.plan/D6.4.md), and
[journal entries](../../../.spec/journal/render-deferred-pbr.md#2026-08-31--d61-unshadowed-point-lighting),
including the [D6.4 bounded point-shadow atlas entry](../../../.spec/journal/render-deferred-pbr.md#2026-09-01--d64-bounded-point-shadow-atlas).

**Done when:** new light types extend render policy and shaders while Asset,
Gameplay, MeshProxy, and the common recorder contract remain stable.

## D7 — evidence and handoff

- [x] Add/update unit and contract coverage for light/shadow handles, material
  schema, target compatibility, and pass filtering.
- [x] Run the validation-matrix targets for changed Render and Graphics code;
  do not run concurrent CMake/MSBuild builds.
- [x] Run Vulkan and OpenGL `GraphicsSmoke`, including capture and teardown,
  then inspect generated PBR screenshots; resize/retirement remains covered by
  the existing target contract tests.
- [x] Update [project status](../../status.md), the design plan, and this
  roadmap with landing dates, exact commands, results, and known visual
  limitations.
- [ ] Revisit the fixed schedule only if evidence shows real dependency,
  aliasing, or scheduling pressure.

## Explicitly deferred

- Alpha-mask/blended deferred geometry, decals, terrain, hair, and material
  graph generation.
- Cascaded directional shadows, shadow atlases, cube-map shadow optimization,
  clustered/forward+ lighting, and graph-driven aliasing.
- Multiple punctual shadow jobs, true cube/cube-array shadow resources, and
  general shadow-atlas allocation until D6.4 supplies a measured baseline.
- Any revival of deprecated OpenGL renderer ownership or native API types in
  common Render/Graphics contracts.
