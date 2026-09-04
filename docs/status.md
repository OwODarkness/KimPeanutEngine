# Project Status

**Snapshot: 2026-09-04.** This is the agent's source of truth for *what state the world is in* — update it as work lands so a future session doesn't re-derive it. Per-module detail lives in the module docs ([asset](asset/asset_module.md), [graphics](graphics/graphics_module.md), [render](render/overview.md), [resource](resource/resource_module.md)); this page is the one-line-per-item index.

## Done

- **Gameplay GP7.1 — level asset review risks resolved (2026-09-01)** — the
  Asset-owned V1 `*.level` schema, strict loader, typed dependency requests,
  transactional unregister revalidation, integer `lod_bias`, path-qualified
  diagnostics, and repeated-reference deduplication coverage are complete.
  The rebuilt focused suite passes 14/14 and the full Debug build plus CTest
  pass 197/197. GP7.3–GP7.5 are now complete and GP7 is closed. → [GP7.1 plan](gameplay/.plan/GP7.1.md),
  [GP7 journal](../.spec/journal/2026-09-01-gameplay-level-asset.md)

- **Gameplay GP7.2 — static-mesh level instantiation and rollback (2026-09-01)**
  — Runtime now owns a dormant non-copyable `LevelInstance` that preflights
  Asset dependency indices, maps model geometry to validated mesh/material
  payloads, creates static-mesh Actors in authored order, and retains only
  generational authored-ID mappings. Creation rollback and unload destroy in
  reverse order, reclaim immediately, retire copied Render sources, and leave
  Asset residency untouched. Focused RuntimeLevel (6/6) and GameplayWorld
  (19/19) tests, full Debug build, and complete CTest (204/204) pass. Bootstrap
  startup remains unchanged until GP7.4. → [GP7.2 plan](gameplay/.plan/GP7.2.md),
  [GP7 journal](../.spec/journal/2026-09-01-gameplay-level-asset.md)

- **Gameplay GP7.3 — heterogeneous level actors and environment source (2026-09-01)**
  — `LevelInstance` now transactionally creates static meshes, directional/point/spot
  lights, and cameras through a closed typed factory set. A Render-owned,
  single-source environment registry accepts only a texture `AssetID` and IBL
  intensity; Render resolves ready texture data at the frame boundary with
  complete fallback preservation and derived-binding reuse. Environment
  registration is last and unload retires it first. Focused GP7.3 tests pass
  40/40; full Debug build and complete CTest pass 220/220; GraphicsSmoke passes
  on Vulkan and OpenGL; and rebuilt-runtime SceneColor captures were exported
  for both backends under `save/screenshots/validation/`. → [GP7.3 plan](gameplay/.plan/GP7.3.md),
  [GP7 journal](../.spec/journal/2026-09-01-gameplay-level-asset.md)

- **Gameplay GP7.4 — startup-level migration and validation fixtures (2026-09-02)**
  — Bootstrap V2 now selects only `level/pbr_showcase.level`; Asset loads the
  complete level/model/material/shader/texture/environment closure, and Render
  resolves authored materials by dependency identity with Render-owned default
  texture warmup. Runtime commits the level instance and possesses its authored
  preferred camera through a ready/commit-or-abort startup handshake. Legacy
  bootstrap scene plumbing and hard-coded startup Actors are removed. The PBR,
  point-shadow, and spot-shadow fixtures launch and capture on Vulkan/OpenGL.
  Full Debug build and CTest pass 222/222; GraphicsSmoke passes on both APIs.
  → [GP7.4 plan](gameplay/.plan/GP7.4.md),
  [GP7 journal](../.spec/journal/2026-09-01-gameplay-level-asset.md)

- **Gameplay GP7.5 — runtime evidence and handoff (2026-09-02)** — the final
  audit added the missing full load→instantiate→unload→unregister dependency
  contract, rebuilt and passed the complete 229-test suite, passed Vulkan and
  OpenGL GraphicsSmoke, and visually inspected fresh PBR, point-shadow, and
  spot-shadow captures on both backends. Bootstrap was restored to the PBR
  level and GP7 is closed. → [GP7.5 plan](gameplay/.plan/GP7.5.md),
  [GP7 journal](../.spec/journal/2026-09-01-gameplay-level-asset.md)

- **Render R1.1 — characterization and transactional lifecycle (2026-09-01)**
  — `RenderSystem` now accepts an injectable factory for the existing
  `RenderBackend` contract, reports initialization diagnostics, rolls back
  partial initialization, and enforces explicit `Uninitialized`, `Ready`,
  `FrameActive`, and terminal `ShutDown` states. Shutdown is idempotent and
  releases frame/target/pass/resolver state before backend cleanup. The new
  `RenderSystemTest` characterizes current target/pass order, conditional
  SceneColor capture, resize waiting, editor terminal composition, rollback,
  and teardown order. Focused lifecycle/render tests and `RuntimeLib` build
  pass. R1.2 extraction is complete; GP7 has since closed.

- **Render R1.2 — deferred renderer and pass-owned state (2026-09-02)** —
  extracted the concrete `DeferredRenderer`, pass-owned targets/state/handles,
  recording and capture conversion from `RenderSystem`, with transactional
  initialization and ordered cleanup. Review findings for partial resource
  retries, ownership probes, stale facade helpers, and the misleading frame
  result were fixed. Focused tests, full Debug build, 239-test CTest, direct
  Vulkan/OpenGL smoke, and fresh startup-level captures passed. → [R1.2 review](render/.review/R1.2.md), [R1.2 journal](../.spec/journal/2026-09-02-render-system-r1-2.md)

- **Render R1.3 — fixed pass declaration/execution unification (2026-09-02)**
  — one immutable eight-entry typed sequence now drives validation and
  deferred-renderer dispatch through a consuming per-frame cursor. Canonical
  ordinal validation and moved-from cursor tests close the review findings;
  full Debug build, 244-test CTest, dual-backend smoke, and fresh PBR,
  point-shadow, and spot-shadow captures passed. → [R1.3 review](render/.review/R1.3.md),
  [R1.3 journal](../.spec/journal/2026-09-02-render-system-r1-3.md)

- **Render R1.4 — ready asset ingestion and Render bootstrap removal (2026-09-02)**
  — Runtime now validates and publishes a selected-API immutable prepared asset
  catalog before Render initialization. The catalog rejects unready or
  mismatched shader artifacts, invalid program/dependency bindings, and
  ordinally drifted built-in roles; const consumers cannot recover mutable
  payloads. An injectable preparation seam covers both backends and
  transactional missing-dependency, built-in, shader, and environment failures.
  Full Debug build and CTest pass 253/253; six PBR/point/spot startup captures
  pass and were inspected. GraphicsSmoke still reports the strict D5
  cross-backend silhouette comparator difference. → [R1.4 review](render/.review/R1.4.md),
  [R1.4 journal](../.spec/journal/2026-09-02-render-system-r1-4.md)

- **Render R1.5 — facade hardening and R1 closure (2026-09-04)** — the stable
  `RenderSceneCoordinator`, typed Graphics-owned Editor bridge, borrowed target
  view, Runtime failed-begin policy, and transactional Editor initialization
  are landed. The comparator now bounds edge/structural differences and rejects
  translated silhouettes and removed thin features with synthetic probes.
  Focused lifecycle tests, full Debug validation, dual-backend GraphicsSmoke,
  and six inspected captures pass. Native orderly Editor close remains an
  environment-limited evidence follow-up. →
  [R1.5 review](render/.review/R1.5.md),
  [R1.5 journal](../.spec/journal/2026-09-04-render-system-r1-5.md)

- **Deferred PBR D6.4 — bounded point-light shadow atlas (2026-09-01)** —
  point shadow intent/handle lifetime, deterministic slot-2 selection, a fixed
  six-face 1536×1024 D32 atlas, deferred point PCF consumption, and point depth
  and visibility diagnostics are implemented. The temporary point-only fixture
  was captured and inspected on Vulkan/OpenGL, then normal bootstrap lighting
  was restored. A one-shot Render profile records per-face/total draws and CPU
  recording time; the fixed D32 target is 6,291,456 bytes and GPU timestamps are
  not available. The follow-up warm sessions recorded identical six-face work
  on Vulkan/OpenGL (`[2,3,1,0,0,6]`, 12 total draws, 2 empty faces, 6
  candidates; 307/177 µs CPU recording), and stable seam-free visibility
  captures. The fixed atlas remains the baseline; true cube resources are
  closed without implementation until a measured benefit appears. Focused tests,
  full Debug build, complete CTest (188/188),
  dual-backend GraphicsSmoke, and rebuilt-engine captures all pass.

- **Deferred PBR D6.3.1–D6.3.4 — bounded spotlight shadow path (2026-09-01)** —
  spot authoring now carries copied `casts_shadow` intent with private
  generational shadow-handle retirement. Render deterministically selects one
  valid `Spot2D` at slot 1, owns a fixed 1024² sampled D32 target, fits a
  perspective light frustum, and records depth casters through the shared
  shadow pipeline. Directional slot 0 and spot slot 1 coexist in the unchanged
  `LightGpuData` stride; deferred lighting applies independent receiver bias
  and 3×3 PCF. Spot depth/visibility capture semantics and runtime command
  names are wired. Spot depth/visibility captures were exported and inspected
  on Vulkan and OpenGL; visibility shows deterministic occlusion while
  perspective depth is near-white after RGBA8 conversion. Focused translation
  and shader syntax checks pass. The normal MSBuild test rebuild remains blocked
  by the environment's denied Windows SDK probe. → [deferred PBR roadmap](render/deferred_pbr/TODO.md)

- **Bootstrap textured PBR scene fixture (2026-08-31)** — the floor now uses
  the oak PBR base-color, normal, roughness, and AO textures. The startup scene
  also includes the Stanford bunny with a quartz material and the teapot with
  a rusted-iron material, plus the Cerberus FBX with its supplied albedo,
  normal, metalness, roughness, and AO maps. This gives the deferred path
  distinct textured material inputs across six render sources. The live
  Vulkan SceneColor capture shows the floor, bunny, teapot, Cerberus, gold
  sphere, and rock rendering together.

- **Deferred PBR D5.7 — environment IBL (2026-08-31)** — the Resource
  pipeline now derives cosine-convolved irradiance, GGX roughness-prefiltered
  radiance, and a BRDF LUT from the Asset-owned RGBA16F HDR panorama; Render
  alone resolves and owns their GPU bindings. The deferred shader keeps visible
  sky radiance separate and applies split-sum diffuse/specular IBL to PBR
  surfaces. A 2D roughness atlas is intentional: current Vulkan common upload
  does not correctly populate cubemap faces/mips. Focused processor/cache tests,
  Debug builds, and dual-backend GraphicsSmoke pass; inspected live Vulkan and
  OpenGL captures show the forest reflected by the gold sphere. Bootstrap's
  optional `environment_intensity` scales material IBL independently of visible
  sky radiance (default `0.25`) to retain directional-shadow contrast. →
  [deferred PBR roadmap](render/deferred_pbr/TODO.md#d57--environment-ibl-landed-2026-08-31)

- **Deferred PBR D5.6 — OpenGL depth-clear state isolation (2026-08-31)** —
  the live OpenGL over-occlusion came from a color-only pipeline leaving
  `GL_DEPTH_WRITEMASK` disabled before the next frame's sampled shadow target
  Clear load-op. `BeginRenderTarget` now enables depth writes for that clear,
  while the subsequently bound pipeline still owns draw state. `GraphicsSmoke`
  primes the stale-state transition as a regression fixture. Full build,
  171/171 tests, direct dual-backend smoke, and inspected live final-color plus
  shadow-visibility captures pass; Vulkan and OpenGL now match. →
  [deferred PBR roadmap](render/deferred_pbr/TODO.md)

- **Deferred PBR D5.5 — HDR environment background (2026-08-31)** — the
  bootstrap scene now preloads `HDR_041_Path.hdr` through Asset. ImageIO emits
  linear RGBA32F for Radiance sources; Asset converts/caps it to 4096-wide
  RGBA16F; Render owns the cached GPU binding and samples the equirectangular
  panorama for clear-depth pixels into `SceneHdr` before the one tone-map pass.
  Vulkan and OpenGL live SceneColor captures both show the forest environment.
  At landing this was background radiance only; D5.7 later added the derived
  irradiance/specular/BRDF IBL path. Full build, 171/171 tests, and direct
  cross-backend GraphicsSmoke pass. → [deferred PBR roadmap](render/deferred_pbr/TODO.md)

- **Deferred PBR D5.4 — runtime diagnostic capture routing (2026-08-31)** —
  Render now resolves SceneColor directly and conditionally converts linear
  depth, world normal, base color, material parameters, and shadow visibility
  into an owned RGBA8 `CaptureOutput` before backend readback. Requests enqueue
  only after the relevant frame work is recorded; Runtime exposes all six
  semantic names and `--graphics-api vulkan|opengl`. Both APIs produced all six
  live PNGs with upright G-buffer/depth views. The new evidence also isolates a
  remaining runtime-only OpenGL shadow regression: its live shadow view is
  fully occluded although the isolated cross-backend D5 smoke remains correct.
  D5 therefore stays open for comparable live final-color proof; HDR/EXR
  capture remains deferred. → [deferred PBR roadmap](render/deferred_pbr/TODO.md)

- **Gameplay GP6.1 — camera data and source contract (2026-08-30)** —
  `CameraComponent` is a transform/lens component with validated perspective
  and orthographic values plus cached world-space basis vectors. It publishes
  copied camera values through a generational `CameraSourceRegistry`; Render
  selects one enabled source by priority and applies it to its private
  `RenderCamera` before culling, shadow fitting, and deferred lighting. The
  GameplayWorld injects the non-owning sink, shutdown clears it safely, and no
  Gameplay type owns matrices, GPU handles, or backend objects. Gameplay and
  Render contract tests pass. Local controller/input traversal is recorded in
  the GP6.2 entry below.

- **Gameplay GP6.2 — local camera traversal (2026-08-30)** — `GameplayWorld`
  now owns one local `PlayerController`; runtime startup creates and possesses
  an active root free camera under the `Gameplay` input context. Keyboard and
  mouse callbacks enqueue copied logical camera actions into a mutex-protected
  frame snapshot, which the game-thread controller consumes for basis-relative
  movement, raw mouse look, pitch/yaw limits, and FOV zoom. Input and Gameplay
  unit tests pass; GP6.3 extends this path with gamepad samples and render smoke
  proof.

- **Gameplay GP6.3 — controller/gamepad expansion and proof (2026-08-30)** —
  Window/GLFW now emits copied platform-neutral gamepad samples once per poll;
  InputSystem selects one active pad, translates sticks/triggers/buttons, and
  applies radial dead-zone, inversion, and sensitivity processing. The local
  PlayerController consumes those logical values on the game thread, including
  disconnect release/zero transitions; InputContext binding lookup/unbind is
  protected across callback and teardown access. Gameplay remains free of GLFW,
  native controller, matrix, GPU, and backend types. `GameplayUnitTest` and
  `InputSystemTest` cover the handoff, dead-zone policy, disconnect behavior,
  stale possession, pitch limits, and unbind. `GraphicsSmoke` drives
  deterministic keyboard/mouse traversal through the camera-source boundary
  and passes on Vulkan and OpenGL.

- **Gameplay GP6.4 — editor viewport camera capture (2026-08-30)** — the scene
  viewport toggles a platform-neutral mouse-capture mode: the first left click
  over the scene hides the cursor and enables camera control, and the next left
  click releases capture and stops camera input. The editor sends only an
  `ISceneCameraControlSink` notification; `RuntimeContext` applies the atomic
  request on the game thread before `GameplayWorld::Tick`. Cursor tracking is
  reset at mode changes, and controller-gating coverage verifies no movement,
  look, or zoom occurs while released. Active input-context processing is also
  gated immediately, preventing stale cursor/key/gamepad actions during a fast
  capture transition.

- **Deferred PBR D5.3 — directional shadow consumption (2026-08-30)** — the
  scheduled `Directional2D` job now promotes only its matching light/shadow
  identities into the GPU frame record. Deferred lighting binds the sampled
  D32 target, frame-local light matrix, clamp sampler, and bias/texel constants,
  then applies 3x3 PCF to direct Cook-Torrance light. Its fixed first fit
  follows the active camera's forward scene focus. Vulkan G-buffer/shadow
  producers now translate shared engine clip depth to Vulkan's range; deferred
  reconstruction handles the backend viewport orientation. The smoke scene
  includes a floor receiver, and Vulkan/OpenGL pass with visually matching cast
  shadows. Portable raster depth bias and explicit shadow capture routing stay
  deferred. → [deferred PBR roadmap](render/deferred_pbr/TODO.md)

- **Deferred PBR D5.2 — directional Cook-Torrance lighting (2026-08-30)** —
  `DeferredLightingPass` now samples the three G-buffer attachments plus D32,
  reconstructs world position with backend-correct NDC depth, consumes the
  versioned frame-light UBO, and evaluates directional GGX/Smith/Schlick into
  RGBA16F `SceneHdr` before tone mapping. Exact std140 size/offset assertions
  lock the CPU/shader ABI; point/spot contribution remains a later slice and
  D5.3 now consumes the directional shadow fields. D5.2.2 moved Y-origin
  normalization from the shared perspective matrix to Vulkan's negative-height
  viewport and normalized OpenGL capture rows. Both APIs now cull the interior
  and render matching exterior/final-lighting captures, providing a valid D5.3
  baseline. → [deferred PBR roadmap](render/deferred_pbr/TODO.md)

- **Deferred PBR D4 — directional shadow depth family (2026-08-30)** —
  `casts_shadow` remains authored Gameplay state while `LightSourceRegistry`
  resolves it to a render-private `ShadowHandle`. `RenderSystem` schedules one
  enabled `Directional2D` job from the immutable light snapshot and records it
  first into a fixed 2048² sampled D32 target using camera-centred orthographic
  light constants and `visible && casts_shadow && opaque && ready` casters.
  The generic depth pipeline consumes only frame-local view/projection and
  model bindings; neither Asset nor Gameplay sees a target, descriptor, or
  native API value. Missing jobs leave the clear-depth map; the existing
  SceneColor debug conversion samples it as a fourth panel. `GraphicsSmoke`
  now validates real depth write → sampled read → capture/resize/teardown on
  Vulkan and OpenGL. Portable depth bias and final shadow-factor evaluation
  remain D5. → [deferred PBR roadmap](render/deferred_pbr/TODO.md)

- **Deferred PBR D3 — Material Asset V2 + opaque PBR G-buffer (2026-08-29)** —
  material assets version to 2 (`kMaterialVersion = 2`, V1 unlit still loads,
  `standard_pbr` rejected in V1, version 3 rejected); the render resolver
  builds a canonical 10-param `StandardPbr` template (base_color vec4@0,
  metallic f@16, roughness f@20, occlusion f@24, emissive vec4@32; samplers at
  bindings 2/5/6/7/8, binding 4 reserved for D5) with per-texture color-space
  intent (`Srgb`/`Linear`, typed texture cache key `{AssetID, color_space}`),
  texture-wins-over-scalar normalization, and two new 1×1 default PNGs
  (`default_white`, `default_flat_normal`). StandardPbr asset loading now
  rejects unknown semantics, type mismatches, non-finite values, and invalid
  numeric ranges. Tangent audit landed: the canonical
  5-attribute layout (pos/normal/uv/tangent/bitangent, 56-byte `data::Vertex`,
  Assimp `CalcTangentSpace`), world TBN `transpose(inverse(mat3(model)))` with
  `mat3(T,B,N)` and `rgb*2-1` decode, with pre-normalization zero guards and
  fragment-stage TBN orthonormalization. The
  G-buffer target is 3-color + D32 — albedo RGBA8_UNORM (linear, {0,0,0,0}),
  normal RGBA16F (raw world-space, {0,0,1,0}), material RGBA8_UNORM (metallic
  R/roughness G/occlusion B, {0,1,1,0}), D32 clear 1.0. `MaterialPass::{Scene,
  ShadowDepth, GBuffer}` threads through pipelines/bindings/proxies/draw lists;
  `RenderSystem` schedules `GBufferPass` (opaque draw lists) →
  `GBufferDebugViewPass` (fullscreen three-panel albedo/normal/material inspection into SceneColor) →
  `EditorCompositePass`, replacing the unlit `ScenePass` (unlit stays green in
  smoke). `GraphicsSmoke` proves the full V2 path on both APIs — real
  `rock_pbr.material` → GBuffer pipeline → write → composite →
  `save/screenshots/validation/graphics-smoke-d3-{vulkan,opengl}.png` with the
  rock framed (camera near=1/far=2000) and non-uniform verified pixels. 148/148
  tests. → [deferred PBR roadmap](render/deferred_pbr/TODO.md)

- **Bootstrap brick surface scene fixture (2026-08-29)** — the bootstrap scene
  now accepts additional mesh/material objects with position, rotation, and
  scale, while preserving the existing primary scene entry. The live scene
  adds the supplied brick-textured `floor.obj` beneath the rock through a
  StandardPbr material; the runtime still only publishes ordinary Gameplay
  static-mesh actors and does not implement shadow rendering. Bootstrap and
  cross-backend smoke validation pass.

- **Deferred PBR D2 — attachment-capable common graphics contract
  (2026-08-29)** — the common RHI now describes multiple named color
  attachments, optional/opt-in-sampled depth, depth-only targets, and
  deterministic target↔pipeline format compatibility. D2 is explicitly
  single-sample until a real multisample/resolve contract exists.
  `RenderTargetDesc` = `std::vector<RenderTargetColorAttachment>` + optional
  `RenderTargetDepthAttachment` (load/store/clear, `shader_readable`);
  `PipelineDesc` semantics relax to empty-color = depth-only and UNKNOW-depth =
  no depth, with the Vulkan auto-fill removed. Both backends translate N
  attachments (Vulkan dynamic rendering `colorAttachmentCount=N`; OpenGL
  `glNamedFramebufferTexture(GL_COLOR_ATTACHMENT0+i)` + `glDrawBuffers` /
  `glDrawBuffer(GL_NONE)`) and now derive depth test/write from the pipeline's
  depth format (no-depth pipelines no longer discard fragments). `RenderBackend`
  exposes per-attachment color/depth accessors without leaking native handles;
  sampled depth requires an explicit opt-in accessor and sample-usage validation.
  Render's `RendererFrameTargets` rebuilds the named `SceneColor` target for one
  extent/format policy (`RebuildForExtent` waits idle, `Cleanup` releases) and
  `RenderSystem` runs init/capture/extent/pass/shutdown through it. `GraphicsSmoke`
  proves multi-attachment (RGBA8+RGBA16F+D32) → sampled readback → depth-only
  lifecycle on Vulkan and OpenGL. Its temporary fullscreen `cull_mode = NONE`
  workaround was retired by D5.2.2 after projection/viewport winding was fixed.
  144/144 tests, engine boots with the migrated viewport. → [deferred PBR roadmap](render/deferred_pbr/TODO.md)

- **Deferred PBR D1.4 — frame lighting ABI (2026-08-29)** — Render packs its
  immutable `LightWorld` snapshot into a bounded version-1 `LightGpuData` UBO
  for every active frame. The 64-record ABI carries directional, point, and
  spot payloads plus enabled/layer data; unscheduled shadows deliberately
  encode as `Unshadowed`. It reserves common set 0/binding 4 for the future
  deferred-lighting pipeline without changing the current unlit descriptor
  layout. Runtime also composes the first default directional `LightActor`
  beside its bootstrap mesh actor. → [deferred PBR roadmap](render/deferred_pbr/TODO.md)

- **Backend-owned command recording (2026-08-29)** — `RenderBackend` remains
  the backend/frame scheduler and resource-service façade, while both backends
  now own a short-lived per-active-frame `CommandRecorder`: the existing
  `VulkanCommandRecorder` and new `OpenglCommandRecorder`. The OpenGL encoder
  borrows only target, pipeline, mesh, descriptor, uniform-upload, and
  bindless services; it does not receive an `OpenglBackend` back pointer.
  Recorder-local target/binding state is invalidated before presentation and
  teardown. → [ownership plan](graphics/command_recording_ownership_plan.md)

- **Command system C0 (2026-08-29)** — added the standalone `RuntimeCommand`
  target with API-neutral structured command types, metadata/schema, origin and
  execution-lane vocabulary, deterministic listing, structured execution, and
  move-only registration tokens. It has no Editor, ImGui, Lua, Render, or
  Graphics dependency; user `~`, agent/headless, and Lua entry points remain
  later frontend work. → [command system plan](command/command_system.md)

- **Command system C1 (2026-08-29)** — the registry now reports duplicate
  providers, exposes built-in `help` and `commands.list`, supports deterministic
  descriptor enumeration, and has explicit shutdown semantics that clear
  registrations and reject future work. Pending domain operations remain owned
  by their handlers. → [command system plan](command/command_system.md)

- **Command system C2 (2026-08-29)** — added the standalone typed parser and
  schema validator for boolean, signed/unsigned integer, float, string, and
  enum values. Text and structured calls now share defaults, required/enum
  validation, stable diagnostics, formatted help, and deterministic completion;
  invalid calls are rejected before handlers run. → [command system plan](command/command_system.md)

- **Command system C3 (2026-08-29)** — command descriptors now declare an
  execution lane, wrong-lane render/async calls are rejected, and Game-lane
  calls from other callers enter a Runtime-owned FIFO. Deferred calls receive
  request IDs with exactly-once callbacks and pollable terminal results;
  development/editor/origin/mutating/destructive policies are enforced, and
  shutdown completes queued requests as `Shutdown`. → [command system plan](command/command_system.md)

- **Command system C4 (2026-08-29)** — Runtime registers
  `capture.screenshot` through a separate screenshot command provider. The
  provider runs through the Runtime game-thread queue, keeps path validation and
  PNG export in `RuntimeScreenshotService`, and translates final capture/export
  state into structured command data. Agent/test callers can use the registry
  without Editor, ImGui, or direct Render/Graphics access; shader reload,
  debug-view, and stats commands remain deferred until their services are real.
  → [command system plan](command/command_system.md)

- **Command system C5 (2026-08-29)** — Runtime now binds WindowSystem key,
  mouse, cursor, and scroll dispatchers into InputSystem. Editor owns a
  detachable ImGui command console toggled by `~`; it uses the Runtime text
  adapter, bounded de-duplicated history, prefix/schema completion, and
  structured pending/final result display. The console is only a frontend:
  command handlers remain in Runtime providers and its input listener is
  removed before Runtime shutdown. → [command system plan](command/command_system.md)

- **Command-system lifecycle hardening (2026-08-29)** — queued Game-lane
  requests now retain and verify their original registration identity, so a
  provider replacement cannot receive stale work. `Pending` requires a
  registry completion sink, shutdown terminally completes every outstanding
  registry request, and the screenshot adapter retains shared service ownership
  for accepted dispatch. → [command system plan](command/command_system.md)

- **Command system C6 foundation (2026-08-29)** — RuntimeCommand now exposes
  the Editor-free `CommandAgentEndpoint` with typed discovery/execution,
  polling, caller-driven cancellation, and JSON-lines transport. The
  `KimPeanutCommand` headless harness hosts built-ins without ImGui; a future
  Runtime-only bootstrap must register graphics-native commands for headless
  capture. → [command system plan](command/command_system.md)

- **Command system C6.1 (2026-08-29)** — Engine now optionally owns a
  loopback-only JSON-lines transport, enabled with `--agent-port <port>`. Its
  dedicated worker transfers bounded requests/responses only; `Engine::GameTick`
  executes commands through the existing Agent endpoint and registry. Startup,
  local-user limits, and teardown are documented, with an automated live-socket
  GameTick-handoff test. A live `capture.screenshot` smoke run returned and
  visually verified the active-frame PNG. → [command transport](command/agent_transport.md)

- **Runtime startup-level selector (2026-09-02)** — the executable now has a
  tested `--startup-level level/<fixture>.level` override with strict Asset-path
  validation, duplicate/unknown/missing-option rejection, CLI-over-Bootstrap
  precedence, and no Bootstrap-file mutation. The selector composes with
  `--graphics-api` and `--agent-port`, so agents can launch a chosen fixture and
  use the existing `capture.screenshot` transport workflow. → [startup-level
  override plan](gameplay/.plan/STARTUP_LEVEL_OVERRIDE.md), [agent transport](command/agent_transport.md)

- **Command system C7 (2026-08-29)** — `Script` now owns
  `LuaCommandBridge`, which exposes `engine.command.list/help/execute/poll/cancel`
  on the Game/Lua thread. It validates Lua tables through the shared registry,
  enforces `LuaAllowed`/capabilities, removes closures before VM shutdown, and
  leaves `RuntimeCommand` Lua-free. Lua unit tests cover pending screenshot
  completion and shutdown cancellation. → [command usage](command/usage.md)

- **Final engine-window capture (2026-09-04)** — `capture.screenshot` now
  accepts `view=engine_window` and completes at the final presentation
  boundary (before OpenGL swap, after Vulkan present) on the
  render thread. Window owns the Win32 client-area capture, including the
  Editor/ImGui composite, while diagnostic views retain the existing GPU
  readback path. → [render usage](render/usage.md)

- **Render capture service contract (C1, 2026-08-28)** — Render now exposes a
  callback-only `IRenderCaptureService` returning owned CPU image values, while
  private `RenderCaptureService` accepts one pending SceneColor request,
  reports reserved debug views as `Unavailable`, and cancels pending work on
  render shutdown. `RenderSystem` owns only the service lifetime; RuntimeContext
  forwards the borrowed service interface. Actual GPU readback remains C2/C3.
  → [capture plans](render/render_capture/PLANS.md)

- **ImageIO codec boundary (C1.4, 2026-08-28)** — the ImageIO Runtime target
  owns normalized RGBA8 CPU image decode and lossless PNG encoding behind one
  stb implementation boundary. Asset calls ImageIO directly, then retains
  texture identity and sRGB format policy; future screenshot export consumes
  the PNG writer without depending on Asset or Render. `ImageIOUnitTest`
  covers buffer validation and a lossless round trip. → [ImageIO module](image_io/image_io_module.md)

- **Runtime screenshot export (C1.5, 2026-08-28)** — `RuntimeScreenshotService`
  composes Render's owned-pixel callback with ImageIO PNG export. It owns UTC
  default names under `save/screenshots/`, restricts explicit outputs to the
  validation directory, and reports file-export completion separately from
  Render. GPU SceneColor capture is still C3 work.

- **Common render-target readback contract (C2, 2026-08-28)** — Graphics owns
  the validated CPU RGBA8 `CapturedImage`, opaque render-target readback
  request/result callbacks, and legal queued/submitted/terminal state
  transitions. It exposes no native image, staging memory, or request handle.
  Vulkan/OpenGL integration is deferred to C3.

- **SceneColor readback on both APIs (C3, 2026-08-28; ownership split
  2026-08-29)** — each backend owns a private service that implements the
  common `IRenderTargetReadback` contract and exposes only its borrowed
  interface. `VulkanRenderTargetReadback` owns image layouts, staging buffers,
  mapped-memory lifetime, pending requests, and callbacks (copy recorded
  pre-submit, collected after the matching frame fence, cancelled before
  resize/shutdown destruction); `OpenglRenderTargetReadback` owns the
  equivalent request/callback state and performs a synchronous
  `glGetTextureSubImage` read at the next frame boundary. `GraphicsSmoke` now requests a
  post-resize SceneColor capture on each API, drains frames until the
  completion callback, and validates the owned RGBA8 image metadata and
  non-uniform pixels; it passes on Vulkan and OpenGL. Visual PNG smoke
  evidence is C4 (below).

- **Runtime PNG export + visual smoke evidence (C4, 2026-08-28)** —
  `GraphicsSmoke` requests the post-resize SceneColor screenshot through
  `RuntimeScreenshotService::RequestScreenshot` with an explicit
  `save/screenshots/validation/graphics-smoke-<api>.png` path, pumps frames
  until the export callback, and validates the written PNG on disk: signature,
  IHDR extent, a decode round trip, and non-uniform pixels. The generated
  `save/` tree is git-ignored. Both backends produce a visually inspectable
  1600x1024 SceneColor PNG and the smoke target fails if capture cannot
  complete.

- **Editor render-capture command (2026-08-28)** — `EditorUI` adds a
  `Tool > Capture Screenshot` menu item bound to `RuntimeScreenshotService`
  (built from `RenderSystem::GetRenderCaptureService`, which targets the scene
  render target). Clicking saves a UTC-named PNG under `save/screenshots/`
  through the runtime export path; success/failure is reported to the editor
  log. `MenuItem` gained an `on_click` command binding (items previously had
  no event binding).

- **Asset module** — two-tier ownership (unique_ptr wrappers, ref-counted payloads), thread-safe (load → state mutex order), content-addressed `path_index`. Refactor complete. → [asset_module.md](asset/asset_module.md)
- **Shader identity + artifact pipeline** — `ShaderProgramLoader` (`.shader` meta → per-stage `ShaderResource`), `ShaderProcessor` + `SPIRVCompiler` (GLSL → SPIR-V, content-addressed cache), `PreprocessOperation` (GLSL → preprocessed source, no cache). Per-API artifact via `ShaderProcessor::keep_source_` → `ShaderData` `byte_code` (Vulkan) or `source` (OpenGL). Wired end-to-end by the asset example; the render module is not — see below.
- **RHI** — Vulkan + OpenGL backends behind `RenderBackend::CreateGraphicsBackEnd`; cross-API handles, `PipelineDesc`, `TextureManager`/`MeshManager`/`SamplerManager`. → [graphics_module.md](graphics/graphics_module.md)
- **Editor UI build boundary (2026-08-26)** — `EditorLib` is now the backend-agnostic editor-tool layer, while `EditorUILib` owns ImGui, GLFW WSI, and the OpenGL/Vulkan ImGui renderers. VulkanSDK, glad, and ImGui are private `EditorUILib` dependencies; the Vulkan bridge remains confined to that module. → [editor module](editor/editor_module.md)
- **Graphics build encapsulation (2026-08-26)** — `Graphics` now keeps its
  backend include root plus `glad`, GLFW, and `VulkanSDK` private; consumers no
  longer inherit native SDK include/link requirements. `USE_OPENGL` and
  `USE_VULKAN` select source lists and factory availability; an OpenGL-only
  `Graphics` build succeeds. `EditorUILib`'s deliberate Vulkan ImGui bridge
  links Vulkan directly. → [graphics module](graphics/graphics_module.md)
- **Common graphics capabilities (2026-08-26)** — `RenderBackend` publishes
  immutable, common-RHI `GraphicsCapabilities` after initialization. Vulkan
  and OpenGL expose their sampled-texture-stage limit without leaking native
  properties; bindless textures deliberately report unavailable until one
  common resource-table contract enables them. → [graphics module](graphics/graphics_module.md)
- **Vulkan bindless sampled-texture table (B2, 2026-08-27)** — Vulkan now
  enables only the descriptor-indexing subset required by the common V1 table,
  when available. Graphics privately owns per-frame descriptor sets, deferred
  slot/resource retirement by completed submission serial, and global set-1
  binding; unsupported devices retain the ordinary bound-resource path. The
  smoke test exercised allocation, frame-boundary update, binding, retirement,
  and Vulkan/OpenGL fallback. → [graphics module](graphics/graphics_module.md)
- **OpenGL bindless sampled-texture table (B3, 2026-08-27)** — OpenGL now
  enables the V1 table only with `GL_ARB_bindless_texture` plus GPU 64-bit
  shader support. The backend owns resident handles in a private SSBO and uses
  a frame fence for deferred non-residency/reuse; unsupported drivers retain
  ordinary texture-unit bindings. → [graphics module](graphics/graphics_module.md)
- **Bindless material adoption (B4, 2026-08-27)** — Material templates can
  opt a compatible shader into the V1 texture-table convention. The resolver
  owns per-instance common slots and falls back atomically to ordinary bindings
  on unavailable capability or allocation failure; `FrameContext` supplies
  the table indices in its material UBO prefix. → [graphics module](graphics/graphics_module.md)
- **Bindless validation and rollout evidence (B5, 2026-08-27)** — The shader
  processor selects target-specific bindless declarations without exposing
  native APIs above Graphics; templates can select a bindless program while
  retaining their ordinary fallback program. `GraphicsSmoke` renders two
  textures through both bindless and bound material variants on Vulkan and
  OpenGL, asserting mode selection and slot coverage. → [graphics module](graphics/graphics_module.md)
- **Runtime bindless selection (2026-08-27)** — The bootstrap scene uses one
  shader-program asset containing ordinary and bindless compile variants.
  Render selects the bindless material path strictly from effective backend
  capability, retaining bound materials as the fallback. → [graphics module](graphics/graphics_module.md)
- **RHI shader/pipeline seam (2026-08-15–20)** — shaders reach the backends as `data::ShaderData`: `PipelineDesc` holds `data::ShaderData*` directly (no `graphics::Shader` wrapper — `Shader`/`ResourceShader`/`ShaderLoader` retired); `CreatePipelineResource(PipelineDesc)` bakes caller-built descriptions into independently destroyable `PipelineHandle`s, while `RenderBackend::Initialize(WindowHandle)` only creates backend/frame state. The path-keyed `ShaderManager` (+ `shader_factory`, `vulkan_shader`, `opengl_shader`) is **retired and deleted**. The `rhi_example` creates two pipeline handles after runtime shader baking. The build-time `glslc` step and the `ShaderModule` seam landed retired 2026-08-16 ([TODO](graphics/TODO.md)). → [vulkanbackend.md](graphics/vulkanbackend.md)
- **Render-owned pipeline warmup (Phase 2 slice, 2026-08-20)** — `RenderSystem` now owns `RenderBackend`, initializes it with the runtime window/resize dispatcher, drives `BeginFrame`/`EndFrame`, and owns a fixed-default pipeline builder/cache. Bootstrap shader programs flow `LoadSync` → `ProcessShader` → `BuildDefaultPipelineDesc` → `CreatePipelineResource`; the cache keys packed program `AssetID`s and retains `PipelineHandle`s until render shutdown. Material-driven states and API-neutral recording remain next. → [render overview](render/overview.md)
- **Static GPU-resource ownership (Phase 3.1, 2026-08-20)** — `RenderBackend` now exposes common mesh/texture/sampler create/destroy APIs; its private managers and Vulkan/OpenGL upload details remain hidden. `RenderSystem` caches handles for queued mesh, model, and texture assets; each `RenderCacheEntry` exposes one type-safe result variant rather than a loose collection of optional handles. `RenderScene` accepts borrowed static resource handles and no longer loads assets, uploads GPU data, or destroys those resources. Vulkan descriptor/command code remains in the scene until Phases 3.2–3.3. → [TODO.md](graphics/TODO.md)
- **Vulkan upload service extraction (Phase 6.3, 2026-08-24)** — `VulkanUploadContext` now owns synchronous staging-buffer creation, one-shot command recording, queue submission/wait, and staging release for buffer and texture uploads. `VulkanBufferManager`/`VulkanMemoryManager` remain the sole allocation owners; the common RHI surface is unchanged. → [TODO.md](graphics/TODO.md)
- **Vulkan editor presentation bridge (Phase 6.4, 2026-08-24)** — `VulkanEditorBridge` now owns only the frame-scoped external ImGui pass: swapchain UI transitions, dynamic-rendering begin/end, and present-layout fallback. `EditorImguiVulkanRenderer` receives initialization data and records via that bridge without calling `VulkanBackend` or retrieving general Vulkan resources; backend ownership of device/swapchain/frame submission is unchanged. → [TODO.md](graphics/TODO.md)
- **Vulkan native escape hatches closed (Phase 6.5, 2026-08-24)** — `VulkanBackend` no longer publishes contexts, queues, pipeline resources, managers, or native command buffers. Vulkan mesh/texture adapters receive direct private buffer-upload/image-memory services through `VulkanContext`; render code remains Vulkan-free and editor Vulkan is confined to the approved ImGui bridge. `GraphicsSmoke` passes Vulkan/OpenGL rendering, resize, and shutdown. → [TODO.md](graphics/TODO.md)
- **Render explicit pass scheduling (Render Phase 1, 2026-08-24)** — `RenderSystem` now owns a validated `ScenePass` → terminal `EditorCompositePass` schedule. The logical `SceneColor` write/read dependency is render-owned and RHI-free; the engine supplies the immediate ImGui callback after input polling, preserving the Render → Graphics dependency direction. `RenderPassScheduleTest` covers accepted ordering and invalid dependency/terminal cases; `GraphicsSmoke` remains green on Vulkan and OpenGL. → [render overview](render/overview.md)
- **Common resource bindings (Phase 3.2, 2026-08-20)** — `RenderScene` now describes its two uniform buffers and sampled texture through handle-only `ResourceBindingSetDesc`s. `DescriptorSetHandle` hides the native implementation: Vulkan privately allocates/updates/binds descriptor pools/sets through `VulkanDescriptorSetManager`; OpenGL stores equivalent binding state. Raw pipeline/mesh draw commands remain Phase 3.3. → [TODO.md](graphics/TODO.md)
- **Minimal cross-API command recorder (Phase 3.3, 2026-08-20)** — `CommandRecorder` exposes pipeline/mesh/resource binding, viewport/scissor, and indexed draw intents during an active frame. Vulkan emits native `vkCmd*`; OpenGL emits equivalent state/draw calls. `RenderScene::Record` now depends only on common handles and the recorder; no Vulkan types or native commands remain in the scene. Phase 3.4 next introduces `FrameContext` for per-frame transient UBO and binding-set lifetime. → [TODO.md](graphics/TODO.md)
- **Frame contexts for transient render data (Phase 3.4, 2026-08-20)** — `RenderSystem` owns one `FrameContext` per backend frame slot and schedules registered scenes inside `BeginFrame`/`EndFrame`. A context owns a 64 KiB aligned uniform arena plus frame-local descriptor sets, recycling both only after the backend waits for that slot’s prior GPU work. `RenderScene` now owns only logical camera/renderable/material state and static RHI handles; UBO ranges and binding sets exist only during its supplied frame context. → [TODO.md](graphics/TODO.md)
- **OpenGL legacy demo ownership removed (Phase 4.1, 2026-08-20)** — `OpenglBackend` no longer loads assets, uses hard-coded demo paths, creates demo UBOs/descriptors, or animates camera/object state. It retains only RHI resource management and `CommandRecorder` translation. → [TODO.md](graphics/TODO.md)
- **OpenGL common execution path (Phase 4.2, 2026-08-20)** — the standalone graphics example creates the same caller-owned `RenderScene`, static resources, and `FrameContext` workflow for Vulkan and OpenGL. `OpenglBackend` validates caller-provided pipeline/resource bindings and translates them through its common command recorder path; API-only differences remain internal. → [TODO.md](graphics/TODO.md)
- **Vulkan/OpenGL parity smoke (Phase 4.3, 2026-08-20)** — the `GraphicsSmoke` executable runs the shared one-object scene for three frames on each API, dispatches a resize event on frame two, exercises Vulkan frame-slot reuse, and completes frame/static/backend/window shutdown with exit code zero. It verifies the no-crash contract, not pixel output. → [TODO.md](graphics/TODO.md)
- **Scene render target + editor viewport (2026-08-21)** — `RenderSystem` owns a render-level `RenderTarget`, backed by API-private offscreen color/depth textures through `RenderBackend`, and brackets every registered scene with `BeginRenderTarget`/`EndRenderTarget`. `RenderTargetView` provides a borrowed, non-owning presentation token for the color attachment; `EditorViewportComponent` consumes it through the `IEditorImguiRenderer` seam. OpenGL displays the texture through `ImGui::Image`; Vulkan presentation still needs its ImGui descriptor bridge. The final swapchain composite remains next. `GraphicsSmoke` exercises target recording on both APIs. → [render overview](render/overview.md)
- **Bootstrap sphere scene (2026-08-21)** — `config/bootstrap.json` explicitly names `simple_triangle.shader`, `model/sphere/sphere.obj`, and `texture/wallpaper.jpg` as its startup scene. The engine passes this policy to `RenderSystem`, which owns and registers a `RenderScene` after those cached pipeline/mesh/material resources are ready. The editor viewport now has a real OpenGL scene to present. → [render overview](render/overview.md)
- **Extensible render initialization (2026-08-20)** — `RenderSystemInitInfo` and `RenderSceneInitInfo` replace positional initialization arguments. Scene static resources are grouped as `RenderSceneResources`; the scene initialization path is API-neutral. → [render overview](render/overview.md)
- **`VulkanDevice` extracted (Phase 1 of the Vulkan decoupling, 2026-08-15)** — landed as a **reconstruction**, not a move: the fused ~2,100-line backend was archived whole to [`backend/vulkan/deprecated/`](graphics/vulkanbackend.md) (git rename, history preserved) and a fresh backend was written that reuses the managers. New `vulkan_device.h/.cpp`: `VulkanDevice` owns instance/debug-messenger/surface/physical/logical device + the three queues + the extension/layer/suitability queries + `QueueFamilyIndices`/`SwapchainSupportDetail`/`RateDeviceSuitability`. The backend holds `std::unique_ptr<VulkanDevice>`, delegates `Initialize(window_)`/`Destroy()`, reads handles via accessors, and fills `context_` from it; `msaa_sampe_count_` is computed in the backend after device init. Dead weight dropped in the rewrite: `VK_CHECK`, the commented-out renderpass/framebuffer + multi-submit blocks. Build green, 86/86 tests. Demo window not re-run (headless) — verify the triangle still draws. → [vulkanbackend.md](graphics/vulkanbackend.md)
- **`VulkanSwapchain` extracted (Phase 2 of the Vulkan decoupling, 2026-08-15)** — a direct move, not another reconstruction (the Phase-1 backend was already clean). New `vulkan_swapchain.h/.cpp`: `VulkanSwapchain` owns the swapchain, its image views, the chosen extent/format and the resize flag (`MarkResized`/`ClearResized`/`HasResized`), with `GetImage(i)`/`GetImageView(i)`/`GetImageCount()`/`GetExtent()`/`GetImageFormat()` accessors. Moved in: `CreateSwapchain`/`CreateSwapchainImageViews` (now `Initialize` + private helpers), the three `Choose*` helpers, and `GetMaxUsableSampleCount`. The backend holds `std::unique_ptr<VulkanSwapchain>` and delegates; `RecreateSwapchain` thins to `DestroyAttachmentResources()` (depth/color textures stay backend-owned) + `swapchain_->Recreate(width_, height_)` + recreate attachments; `CleanupSwapchain` thins to the same texture destroy + `swapchain_->Cleanup()`; `FramebufferResizeCallback` delegates to `swapchain_->MarkResized()`. Build green, 86/86 tests. Demo window not re-run (headless) — verify the triangle still draws and still resizes. → [vulkanbackend.md](graphics/vulkanbackend.md)
- **`VulkanFrameContext` extracted (Phase 3; current shape 2026-08-24)** — `VulkanFrameContext` owns graphics/transfer command pools, per-frame scene command buffers, semaphores/fences, in-flight indexing, sync2 image transitions, and wait/acquire/reset/submit/present plumbing. Phase 6.3 moved one-shot allocation/submit/wait into `VulkanUploadContext`; Phase 6.4 deleted unused UI command buffers and delegates the external ImGui pass to `VulkanEditorBridge`. It still rebuilds frame × image-count render-finished semaphores after swapchain recreation. → [vulkanbackend.md](graphics/vulkanbackend.md)
- **Scene recording extracted (Phase 4; current shape 2026-08-24)** — `RenderScene` records API-neutral intent through `CommandRecorder`; Vulkan-native command encoding is private to `VulkanCommandRecorder`. `VulkanBackend` no longer publishes a scene command buffer: it coordinates the frame, while the editor uses the constrained `VulkanEditorBridge` external-pass callback. Static resources remain render-owned and frame data remains in `FrameContext`. → [vulkanbackend.md](graphics/vulkanbackend.md)
- **`RenderBackend` facade cleanup (Phase 5 of the Vulkan decoupling, 2026-08-16)** — the last documented phase. The `GLFWwindow *window_` test seam and the dead public `CameraData camera_data` member are gone from the cross-API facade; `RenderBackend::Initialize` now takes the native window handle (`WindowHandle` = `void*`) as an explicit parameter, and each backend casts it back to `GLFWwindow*` internally (the editor's `EditorImguiGLFWWSI` cast pattern). **Sakura split decided (TODO 3.2): keep the frame loop** — the device/frame separation already lives inside the backend (`VulkanDevice` = pure device + queues; `VulkanSwapchain`/`VulkanFrameContext` = frame lifecycle), so a frame executor above the facade would re-fuse what Phases 1–3 separated with no consumer that needs it. `Graphics`/`Render`/`RuntimeLib` build clean. *Full build + tests not re-run here: the working tree is missing the prebuilt `third_party/googletest` libs (all test targets fail on `gtest/gtest.h`), the vendored imgui backends are newer than the in-tree core (`ImTextureData` undeclared, `EditorLib` fails), and `tts_example.cpp` hits a codepage error — all pre-existing, unrelated to this change.* → [vulkanbackend.md](graphics/vulkanbackend.md)
- **Build-time `glslc` step removed (TODO 2.3 of the decoupling, 2026-08-16)** — `Graphics/CMakeLists.txt` no longer finds `glslc` or precompiles `.vert/.frag` → `.spv`; the `Shaders` custom target is gone, so `glslc` is no longer a configure-time build requirement. The `rhi_example` demo now bakes its shaders at runtime through the resource pipeline (`asset.LoadSync(simple_triangle.shader)` → `ProcessShader` → `ShaderData`), the flow the asset example proves — giving `ResourcePipeline::ProcessShader` its first **graphics-end** caller and unifying the demo across Vulkan + OpenGL (the pipeline fills `byte_code` or `source` per API). The dead `GetSPVShaderDirectory()`/`binary_root`/`PROJECT_BINARY_DIR` path helpers went with it. Build green, 37/37 tests. Demo window not re-run (headless) — verify the triangle still draws. → [vulkanbackend.md](graphics/vulkanbackend.md)
- **Audio + TTS modules** — miniaudio system, buffer player, GPT-SoVITS client. Streaming playback (`StreamAudioPlayer`) fixed for startup stutter: first chunk decodes immediately, ring buffer refills via a low-water `FillBuffer`/`Refill`, temporary underruns output silence instead of stopping, and the FIFO dry-read no longer tears the player down. → [audio_module.md](audio/audio_module.md) (includes the stutter bug history), [tts_module.md](tts/tts_module.md)
- **Window icon** — `GLFW_WindowSystem::Initialize` sets the taskbar/window icon from `config/icon.png` (via `GetIconPath()` + `glfwSetWindowIcon`, decoded with stb_image). Non-fatal if the file is missing. `stb_image` became a compiled static lib (was header-only INTERFACE with `STB_IMAGE_IMPLEMENTATION` in a shared header — that caused duplicate-symbol link errors once a second TU included it); the implementation now lives once in `third_party/stb_image/stb_image_impl.cpp`.
- **Unit tests** — audio decode + bootstrap parser/request builder + Lua VM + profile.
- **Module design docs** — asset, graphics, render, resource, audio, tts, script, editor written. → [resource_module.md](resource/resource_module.md) (CPU-side processing layer: compiles/bakes, does not load or touch GPU), [editor_module.md](editor/editor_module.md) (editor as core center; WSI/renderer seams keep ImGui decoupled from GLFW/graphics API; composable `EditorUIComponent` tree).
- **Script module — Lua VM hosting layer** — relocated `engine/script/` → `engine/runtime/script/` and wired into `RuntimeLib` (was an orphaned top-level module). `LuaVM` (lib `ScriptLua`) is now an engine-agnostic sol2 wrapper: non-throwing API (bool + `LastError`, `std::optional` lookups), real `Initialize`/`Shutdown` state lifecycle, sandboxed library set (`os`/`io`/`debug`/`coroutine` unopened, `package.loadlib`/`cpath` stripped), per-execution instruction budget (runaway scripts abort instead of hanging the game thread), `SOL_ALL_SAFETIES_ON`. Unit-tested headless under `ScriptUnitTest`. **The engine owns a live instance:** `RuntimeContext::lua_vm_` is created + `Initialize`d at boot (`RuntimeContext::Initialize`) and released in `Clear()`. The `Script` lib is the seam for the future binding layer. → [script_module.md](script/script_module.md)
- **Editor restored (minimal)** — the engine owns the editor again (`Engine::editor_`, created in the ctor, ticked/cleared by the engine loop). `Editor` → `EditorUI` renders **two ImGui windows** — a "window" + "hello imgui" label, plus the **OutputLog** log window (wired 2026-08-13) — no scene manager / actor panel yet. Decoupling preserved: `IEditorImguiWSI` (GLFW) + `IEditorImguiRenderer` (GL/Vulkan, chosen by `GraphicsAPIType`) — ImGui never binds to a specific API. ImGui's context, WSI + renderer init and shutdown live on the **render thread** (`InitEditorUI`/`CloseUI`), where the GL/Vulkan context exists; the render tick is now input-poll → editor tick → swap so the UI renders the same frame. `EditorContext` (`global_editor_context`) was fixed up: `render::RenderSystem` type corrected, deleted `WorldSystem` dropped. The GL editor renderer loads `glad` itself (the legacy GL backend that used to own it isn't in the build) and owns the per-frame clear (`0.1` gray) as a stopgap until the reconstructed scene renderer returns. Rendering verified: the presented frame (`GL_FRONT`) matches the ImGui-rendered back buffer after `SwapBuffers`. → [editor_module.md](editor/editor_module.md)
- **Editor directory and target split (2026-08-13, updated 2026-08-26)** — headers sit beside their sources under `engine/editor/`, grouped by concern: `context/` (hub), `ui/` + `ui/component/` (UI manager + widget tree), `platform/` (WSI/renderer backends), `log/`, `settings/`. All editor includes use the engine root (`"editor/..."`), matching the runtime convention. `EditorLib` now contains only editor-tool orchestration and `EditorContext`; `EditorUILib` owns UI/platform sources and its private ImGui, glad, and VulkanSDK dependencies. Six all-comment dead files were deleted (`EditorSceneManager`, `EditorActorControlPanel`, scene/camera component impls), and the redundant `EditorLogManager` was removed when the log window joined the UI tree. Every subdirectory lists sources explicitly through `target_sources` (no `file(GLOB)`); `main.cpp` is compiled by the root exe target only. Builds clean.
- **Configurable editor settings (2026-08-13)** — `config/settings.json` (beside `bootstrap.json`) now drives the log window's per-`LogLevel` entry colors instead of the hard-coded `ExtractTipColorFromLogLevel` switch. `GetSettingsPath()` in `config/path.h`; `EditorSettings`/`ReadEditorSettings`/`DefaultLogColors` are in the `EditorUILib`-owned `engine/editor/settings/` module (the parser mirrors `ReadBootstrap`'s tolerance: missing file throws, malformed/missing entries warn + fall back to defaults). `EditorUI::Initialize` loads the colors with a try/catch fallback, and `EditorLogComponent` takes a `LogLevelColorTable` (indexed by `program::LogLevel`) instead of switching on level. Also fixed three stale `BootstrapTest` path assertions that expected relative paths from `BuildLoadRequests` (it returns absolute `GetAssetDirectory() + path`). → [editor_module.md](editor/editor_module.md)
- **Editor profile bar (2026-08-13)** — a bottom status bar showing FPS, frame ms, and memory. Two decoupling seams: `EditorMetric` (the extension point — implement `Name()`/`Sample()`, or wrap sampler lambdas in `EditorFuncMetric`) and `EditorProfileBarComponent` (samples injected metrics and draws them in one row; it only ever talks to `EditorMetric`). Built-ins: FPS (engine via an injected sampler), frame time (derived from fps, not a self-measured clock — that would see the render loop's pacing sleep), memory (process + system free via an injected stats sampler). **Measurement lives in the platform layer, not the editor and not the engine** — FPS stays in the engine (`Engine::GetFPS`, it's game-loop timing), but memory is an OS query and lives behind a platform seam: `MemoryStatsSampler` interface + `WindowsMemoryStatsSampler` under `runtime/platform/win/` (PSAPI/GlobalMemory, `psapi` linked into the `Platform` lib), owned by `RuntimeContext`, reached by the editor through `EditorContext`. The engine is platform-agnostic again. Plot-capable metrics draw a small sparkline via the base's history buffer. `EditorUI::Initialize` now takes an `EditorUIInitInfo` bundle (window, backend, log system, engine, memory sampler — defaulted, mirrors `EditorContextInitInfo`) so the signature doesn't grow with each injected dependency. Unit-tested under `ProfileTest` (5 cases, direct-compile; no Win32 in the test). → [editor_module.md](editor/editor_module.md)

## In progress / built but not wired


- **Directional-shadow caster correctness (2026-08-31)** — `ShadowDepthPass`
  now iterates the complete immutable RenderWorld snapshot rather than culling
  casters by its fixed, camera-derived light frustum. It still accepts only
  visible, opaque, ready proxies marked `casts_shadow`. The directional light
  now fits its orthographic volume to the complete caster world bounds and
  includes the camera position as a conservative receiver anchor, so an
  off-camera or distant caster is not GPU-clipped before it can shadow a
  visible receiver. Camera-frustum culling remains exclusive to `GBufferPass`.
  This is intentionally scene-wide and may reduce shadow-map density; tighter
  receiver-aware fitting/cascades remain later work. → [deferred PBR roadmap](render/deferred_pbr/TODO.md#d4--directional-shadow-pass-family)

- **Deferred PBR D6.1 — unshadowed point lighting (2026-08-31)** — Gameplay
  now publishes `PointLightSourceDesc` through the same opaque light-source
  token model as directional lights. Render resolves the source into its
  existing `LightType::Point` record without allocating a `ShadowHandle`; the
  deferred shader uses the already-versioned position/range fields for
  inverse-square attenuation with a smooth quartic range cutoff. The bootstrap
  scene creates one warm point-light actor. This leaves Asset, MeshProxy,
  material bindings, common RHI contracts, and punctual-shadow scheduling
  unchanged. → [deferred PBR roadmap](render/deferred_pbr/TODO.md#d6--light-and-shadow-expansion)

- **Deferred PBR D6.2 — unshadowed spot lighting (2026-08-31)** — Gameplay
  now publishes a copied spot source with position, travel direction, range,
  inner cone, outer cone, color, intensity, and enabled state. Render resolves
  it to its pre-existing `LightType::Spot` GPU record without a shadow handle.
  Deferred lighting combines D6.1 range attenuation with a squared cosine cone
  factor, and the bootstrap scene creates a blue spotlight. The subsequent
  D6.3 slice adds the bounded `Spot2D` target, filtering, scheduling, and
  diagnostics. → [deferred PBR roadmap](render/deferred_pbr/TODO.md#d6--light-and-shadow-expansion)

- **Deferred PBR D5.1 — HDR presentation spine (2026-08-30)** — Render now
  owns a sampled RGBA16F `SceneHdr` separately from the stable RGBA8 sRGB
  `SceneColor`. The validated fixed schedule runs the current G-buffer/shadow
  diagnostic conversion into HDR, then a fullscreen global-Reinhard
  `ToneMapPass` into SceneColor before the editor composite. Cross-backend
  smoke proves the target/pipeline/descriptor chain and captures
  `graphics-smoke-d5-{vulkan,opengl}.png`. Deferred Cook-Torrance lighting,
  directional shadow-factor evaluation, explicit capture-view routing, and
  runtime command-path screenshots remain D5 work. →
  [deferred PBR roadmap](render/deferred_pbr/TODO.md#d51--hdr-presentation-spine-landed-2026-08-30)

- **Deferred PBR D1.3 — typed light/shadow descriptions (2026-08-29)** —
  `LightWorld` now stores validated `LightDesc` records: common
  color/intensity/enabled/layer/shadow identity plus type-matched directional,
  point, or spot data. Render-private `ShadowHandle`, `ShadowKind`, and
  `ShadowJobDesc` establish source-light, resolution, and binding-slot
  identity without allocating a target or exposing it to Gameplay/Asset.
  Contract tests reject type/payload mismatches, invalid spot cones, invalid
  jobs, forged/stale handles, and incompatible light/shadow kinds.
  `RenderPassScheduleTest` and Vulkan/OpenGL `GraphicsSmoke` pass. D1.4 owns
  the frame GPU layout; D4 owns job scheduling and depth targets. →
  [deferred PBR roadmap](render/deferred_pbr/TODO.md#d13--extensible-light-and-shadow-description)

- **Deferred PBR D1.2 — Render light snapshots (2026-08-29)** — Render now
  owns `LightSourceRegistry` and `LightWorld`. The registry accepts copied
  Gameplay source commands under its inbox lock, maps only source registrations
  to private generational `LightHandle`s while draining in
  `RenderSystem::BeginFrame`, and applies them to `LightWorld`. Passes receive
  copied, deterministically ordered snapshots; they cannot read a Gameplay
  component. Contract coverage proves create/update/destroy order, stale
  source-token rejection, resolved-handle retirement, and shutdown clear.
  `RenderPassScheduleTest` and dual-backend `GraphicsSmoke` pass. Point/spot
  ABI and shadow state remain D1.3. →
  [deferred PBR roadmap](render/deferred_pbr/TODO.md#d12--render-light-snapshot-resolution)

- **Deferred PBR D1.1 — Gameplay directional-light source (2026-08-29)** —
  Gameplay now owns `DirectionalLightComponent` and the
  `CreateDirectionalLightActor` composition. It publishes copied direction,
  color, intensity, and enabled values through Render's narrow
  `ILightSourceSink`, retaining only an opaque source-registration token for
  update/destruction. The component has no `LightHandle`, shadow target,
  descriptor, Graphics type, or direct RenderWorld access. `GameplayUnitTest`
  covers create/coalesced-update/destroy and factory lifecycle. Render's
  mailbox, resolved LightWorld handles, and immutable snapshots remain D1.2.
  → [deferred PBR roadmap](render/deferred_pbr/TODO.md#d11--gameplay-light-source-publication)

- **Mesh proxy foundation (MP1 + basic MP2 + CPU frustum visibility, 2026-08-26)** — `RenderWorld`,
  owned by `RenderSystem`, accepts value-only create/update/destroy commands,
  applies them at the frame boundary, and returns immutable `MeshProxy`
  snapshots behind generational `RenderableHandle`s. The ScenePass now draws
  every visible ready proxy through Material V1 and `FrameContext`; the old
  bootstrap `RenderScene` path is retired from the engine. `SceneVisibility`
  conservatively builds the ScenePass list by rejecting proxies whose shared
  `spatial::AABB`s are outside the camera frustum; malformed bounds stay
  visible. `CoreSpatial` owns this bounds value for future World, Physics,
  Render, and editor consumers. World
  partition, LOD, occlusion culling, shadow classification, and transparent
  depth sorting are deliberately deferred. Opaque work is sorted by resolved
  pipeline, material instance, then mesh before ScenePass recording.
  → [mesh proxy TODO](world/mesh_proxy_TODO.md)

- **Material System V1 (M1–M4, 2026-08-26)** — Render owns real generational
  template and instance handles, immutable surface-template descriptors, and
  typed sparse instance overrides through compact parameter IDs; `MeshProxy`
  carries that real material handle. `MaterialSystem` now reports pending,
  ready, or failed resolution while the private resolver owns common pipeline,
  texture, and sampler handles. `FrameContext` now turns a ready instance into
  transient constant/texture bindings, and the bootstrap scene uses that real
  handle rather than a raw texture binding. `GraphicsSmoke` passes on Vulkan
  and OpenGL, including resize and teardown. → [material plans](render/material_system/PLANS.md),
  [material TODO](render/material_system/TODO.md)

- **Render resource resolver extraction (2026-08-25)** — `RenderSystem` now
  coordinates resource requests but no longer implements static RHI resource
  creation/caching itself. Its private `RenderResourceResolver` owns the
  default pipeline, mesh, texture, and sampler caches and releases their
  handles before backend teardown. This is the M3 integration seam for
  `MaterialSystem`; it exposes neither `RenderBackend` nor native API objects.
  → [render overview](render/overview.md)

- **Graphics contract hardening (2026-08-24)** — `GraphicsContractTest` now
  covers stale/forged handle rejection, `PipelineDesc` validation, and Vulkan
  shared-block range merge/reuse without a GPU. `GraphicsSmoke` passes the same
  `RenderScene` through Vulkan and OpenGL, including resize, dedicated mapped
  buffer allocation, and teardown. The remaining graphics test debt is a
  pipeline-cache equality test plus conditional non-coherent-memory hardware
  coverage. → [graphics TODO](graphics/TODO.md)

- **`ResourcePipeline::ProcessShader` has callers on both ends now** — the asset example bakes `simple_triangle` GLSL → SPIR-V end-to-end, and the `rhi_example` demo (2026-08-16, TODO 2.3) bakes its shaders through the pipeline at startup, replacing the build-time `glslc` step. Nothing reads prebuilt `.spv`/`.vert` files anymore. The graphics end **consumes `ShaderData`** as `data::ShaderData*` in `PipelineDesc` (2026-08-15); the render module itself still isn't wired.
- **Render module reconstruction** — `RenderSystem` owns the API-neutral `RenderBackend`, default `PipelineDesc` warmup/cache, and frame lifecycle. It still lacks material-defined state, a scene graph, and API-neutral recording; `RenderScene` remains the Vulkan-specific demo seam.

## Planned (next up)
- **Asset loading progress screen (LO1, LO2 core, and LO3 UI landed 2026-09-04)** —
  Asset now exposes opt-in, session-scoped load observations with recursive
  operation correlation, bounded immutable snapshots, timing/size facts,
  cache/dedup dispositions, failure diagnostics, and async sealing safety.
  Runtime now publishes staged presentation/scene startup state and keeps the
  Editor presentation alive while startup work proceeds. The loading view now
  consumes copied snapshots and transitions once to the main scene UI; runtime
  visual evidence remains. → [architecture map](asset/PLANS.md),
  [roadmap](asset/TODO.md), [cross-stage spec](../.spec/specs/asset-loading-progress.md),
  [LO1 journal](../.spec/journal/asset-loading-progress.md)
- **Gameplay editor inspection (deferred)** — Gameplay is game-thread-owned,
  while the current editor runs on the render thread. Add a read-only snapshot
  before exposing Actor/component state to editor tools; do not give Editor
  mutable GameplayWorld ownership. → [gameplay design](gameplay/gameplay_module.md)
- **Gameplay boundary contract (GP0, 2026-08-28)** — the `Gameplay` Runtime
  target now owns `ActorHandle`/`ActorState`; Render owns the header-level
  `IRenderableSourceSink`, generational source token, and static-mesh source
  descriptor variant. Gameplay links only Core and Render, while Graphics stays
  Render-private. Runtime smoke and editor follow-up remain GP5. →
  [gameplay TODO](gameplay/TODO.md)
- **Gameplay World/Actor/component ownership (GP1, 2026-08-28)** —
  `GameplayWorld` owns generational Actor records with deferred storage
  reclamation; Actor uniquely owns its components and drives their one-time
  initialize, ordered activation/tick, and reverse-order deactivation. Actor
  destruction invalidates the handle immediately. `GameplayUnitTest` covers
  lifecycle order, duplicate/late-add policy, stale handles, and teardown.
  → [gameplay design](gameplay/gameplay_module.md),
  [gameplay TODO](gameplay/TODO.md)
- **Gameplay scene transforms and primitive state (GP2, 2026-08-28)** —
  same-Actor SceneComponent attachments reject self/cycles and keep cached
  transforms correct through `parent_world * local_transform`. Primitive state
  consists only of visible/casts-shadow flags and local/world AABBs; it remains
  headless and has no RenderWorld ownership. `GameplayUnitTest` covers parent
  changes, detach, invalid attachment, transform composition, and bounds. →
  [gameplay TODO](gameplay/TODO.md)
- **Gameplay MeshComponent source production (GP3, 2026-08-28)** —
  GameplayWorld injects a non-owning Render source sink; MeshComponent emits
  value-only static-mesh create/update/destroy requests and retains only the
  generational source token. It coalesces dirty state to one update per tick;
  no Gameplay type can reach RenderWorld, MeshProxy, or Graphics. The eight
  `GameplayUnitTest` cases include this command lifecycle. →
  [gameplay design](gameplay/gameplay_module.md),
  [gameplay TODO](gameplay/TODO.md)
- **Gameplay/render bridge integration (GP4, 2026-08-28)** — RenderSystem owns
  the mutex-protected source sink and render-thread source records. BeginFrame
  resolves ready logical mesh/material values into queued MeshProxy changes
  before RenderWorld applies them; pending/failed records have no proxy.
  RuntimeContext owns and ticks GameplayWorld before the game-to-render
  handoff, then destroys it before RenderSystem shutdown. Focused source and
  gameplay tests pass; runtime graphics smoke remains GP5. →
  [gameplay design](gameplay/gameplay_module.md),
  [gameplay TODO](gameplay/TODO.md)
- **Gameplay validation (GP5, 2026-08-28)** — `GameplayUnitTest` and the
  render-source registry contract test cover source lifecycle. `GraphicsSmoke`
  passed the gameplay mesh create/move/visibility/destroy/resize/teardown path
  on Vulkan and OpenGL (three frames each). Editor inspection is deliberately
  deferred pending a read-only gameplay snapshot. →
  [gameplay TODO](gameplay/TODO.md)
- **Bootstrap Gameplay Actor migration (2026-08-28)** — Render startup now
  prepares a logical static-mesh source after loading bootstrap assets and
  creating the render-owned material identity. The startup handshake transfers
  that value to the game thread, where `CreateStaticMeshActor` creates the
  normal World-owned Actor; `RenderSystem` no longer owns a special bootstrap
  `MeshProxy`. → [gameplay design](gameplay/gameplay_module.md)
- **Material Asset V1 — asset loading slice (2026-08-28)** — Asset now loads
  strict version-1 `*.material` JSON into CPU-only `MaterialResource` values:
  material-relative shader/texture paths, surface policy, and scalar/vector/
  texture parameter sources. The format is documented beside the asset module;
  Render-side resolution is recorded in the following M6 completion entry.
  → [asset file structure](../engine/runtime/asset/README.md),
  [material TODO](render/material_system/TODO.md)
- **Material Asset V1 — render resolution (2026-08-28)** — Gameplay now
  publishes material AssetIDs, while RenderSystem resolves each loaded material
  into one cached private template/default-instance pair and supplies only that
  instance to MeshProxy. Bootstrap now references `bootstrap.material`; the
  initial unlit texture convention is `base_color_texture` at binding 2.
  → [material TODO](render/material_system/TODO.md)
- **Material Asset V1 — M6.1 validation (2026-08-28)** — the Render-owned
  material-asset cache is independently testable. Focused tests cover
  deduplication, invalid/unloaded/broken references, schema-version rejection,
  and failed-source proxy retirement; Vulkan/OpenGL graphics smoke passed.
  → [material TODO](render/material_system/TODO.md)
1. **Render module reconstruction** — follow the structured [render overview](render/overview.md) and its [preserved roadmap](render/render_module.md#render-roadmap): preserve the Render → Resource → Graphics boundary while evolving passes, scene policy, and validation.
2. **RHI leak fixes** — `ShaderManager` retired + `PipelineDesc` shaders backed by `ShaderData` (**landed 2026-08-15**, Phase 0 of [vulkanbackend.md](graphics/vulkanbackend.md)). **`VulkanDevice` extracted (landed 2026-08-15**, Phase 1 — reconstruction; original archived at `backend/vulkan/deprecated/`). **`VulkanSwapchain` extracted (landed 2026-08-15**, Phase 2). **`VulkanFrameContext` extracted (landed 2026-08-15**, Phase 3 — command pools, scene/UI buffers, sync objects, in-flight index, one-shot primitives, sync2-only barriers; shared one-shot buffers + dead transfer helpers deleted). **Scene recording extracted (landed 2026-08-15**, Phase 4 — the backend exposes "the current frame's command buffer + attachments"; the demo moved out to `render::RenderScene`, the render module's first real scene; TODO 5.1 `Render` links `Graphics` landed). **Facade cleanup (landed 2026-08-16**, Phase 5 — `window_`/`camera_data` public seams dropped, `Initialize` takes the native window handle; sakura split decided: keep the frame loop). **Build-time `glslc` step removed (landed 2026-08-16**, TODO 2.3 — the demo bakes shaders at runtime via `ProcessShader`). **`ShaderModule` seam retired (landed 2026-08-16**, TODO 1.2 — raw `ShaderData` → API object stays inline in the pipeline bakes).
3. **Resource pipeline gaps** — add `CompileFailed` status (carry error text); make `ProcessShader` take the whole `ShaderProgramResource` as one compile unit.
4. **Headless unit tests** — asset manager, `GenerateShaderHash`, `ShaderCache`, `HandleSystem` are all testable without a GPU.
5. **Async resource queue** — superseded by R1.4. The former request-based
   path consumer had no production producer and was removed. Generic
   `AsyncQueue<T>` remains available for future, independently justified
   transport; future streaming must move ready typed payloads, not paths.
   → [async_resource_queue.md](async/async_resource_queue.md)
6. **Bootstrap preload pipeline** — Engine boot selects and synchronously loads
   the startup level, then Runtime prepares the immutable Render catalog before
   the Render thread initializes. The old async request flow is retired; the
   selected level remains the authored root and built-in pass assets stay
   renderer-owned requirements.
   The bootstrap remains an engine-scoped selector and validates the startup
   level path; the Runtime preparation transaction now performs the Asset and
   Resource work and publishes the catalog before the render thread starts.
7. **Script binding layer follow-up** — C7 landed the native command bridge;
   remaining engine-facing work is broader `kpengine` class bindings, rooting
   `package.path` at `GetScriptDirectory()` → `asset/script/`, asset-pipeline
   script loading, and a `ScriptSystem` owned by `RuntimeContext`. →
   [script module](script/script_module.md)

## Known broken / known issues

- **Concrete scene recording remains Vulkan-only** — `RenderSystem` now owns the common backend/pipeline lifecycle, but `RenderScene` still uses raw Vulkan commands. Phase 3 replaces that seam with common recording commands.
- **`main.cpp` selects examples by uncommenting** — most examples block (windows, `while(1)`); running the binary from an agent shell will hang.

## Dead code & stale paths

→ [docs/dead_code.md](dead_code.md). Everything that is not in the build, not wired, or slated for retirement lives there so it doesn't get "fixed" as if live.
