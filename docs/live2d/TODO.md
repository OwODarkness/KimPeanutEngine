# Live2D Module Roadmap

**Status: proposed.** Architecture and reference analysis live in
[PLANS.md](PLANS.md). The cross-stage V1 contract is
[live2d-v1-rendering](../../.spec/specs/live2d-v1-rendering.md).

## V1 — import and render one Live2D model

- [x] **L2D1 — Cubism SDK integration**
  ([plan](.plan/L2D1.md)): added the optional, development-default-on,
  version-pinned Cubism 5 Native R5
  build boundary; validated Core/Framework compatibility, allocator/log bridge,
  repeated lifecycle, model-instance leases, and explicit SDK-off builds.
  Live2D is enabled by default for development; use `OFF` for SDK-free builds.
  The editor now contains a temporary aspect-fit animated placeholder panel;
  Cubism-backed model rendering remains below.
  → [L2D1 journal](../../.spec/journal/2026-09-08-live2d-l2d1.md)
- [x] **L2D2 — Live2D Asset integration** ([plan](.plan/L2D2.md)): consume the
  generic Asset extension contract after AX1. Register the Live2D type,
  `Live2DModelResource`, `.live2d` native loader, and `.model3.json` offline
  importer without adding Live2D names or branches to Asset. L2D2.0–L2D2.4
  are landed; renderer integration
  remains in later stages.
- [ ] **L2D3 — native Live2D product and importer:** import a safe
  `.model3.json` source closure into deterministic `.live2d` bytes plus native
  Texture dependencies; load the immutable payload through ordinary
  `AssetManager` without source import or archive writes.
- [ ] **L2D4 — model instance and common renderer:** create independent mutable
  instances, add frame-safe streaming geometry to the common RHI, implement
  draw order/color/blend/culling/mask rendering, and validate both OpenGL and
  Vulkan implementations.
- [ ] **L2D5 — dedicated viewer and V1 evidence:** add
  `KimPeanutLive2DViewer`, resize and capture support, a legally usable fixture,
  official-reference comparison, cross-backend screenshots, and clean shutdown
  evidence.

## V1 acceptance

- [ ] Live2D is enabled by default for development, while
  `KPENGINE_ENABLE_LIVE2D=OFF` allows the ordinary engine to configure/build
  without a Cubism SDK installation.
- [ ] Enabling Live2D with a missing, mixed-version, or unsupported Core package
  fails during configuration or initialization with a precise diagnostic.
- [ ] Asset and AssetImport contain no Live2D include, type declaration, loader
  branch, or source-suffix branch.
- [ ] Live2D uses the Asset-owned polymorphic payload and registration path;
  Asset contains no Live2D name, include, payload, suffix, or loader branch.
- [ ] Live2D integration tests cover payload type agreement, native loader
  registration, texture dependency handling, rollback, and unloading through
  ordinary AssetManager transactions.
- [ ] Import rejects path escape, malformed JSON, missing `.moc3`/texture data,
  unsupported required features, and corrupt products without publishing a
  partial root.
- [ ] One native `.live2d` asset can create at least two independent model
  instances that share immutable data and textures but not parameter state.
- [ ] One representative clipped model renders with correct order, opacity,
  normal/add/multiply blending, masks, canvas transform, and transparent
  background on OpenGL and Vulkan.
- [ ] The dedicated viewer resizes, captures a stable frame, and exits without
  Graphics validation errors, leaked Cubism objects, or live GPU handles.
- [ ] Visual evidence is compared against the official R5 renderer with
  documented tolerances; compilation alone is not accepted.

## Post-V1 roadmap

- [ ] **L2D6 — authored motion and expression playback:** expose named motion
  groups, expression layers, fades, priorities, cancellation, and completion
  without embedding policy in the shared asset.
- [ ] **L2D7 — secondary model behavior:** physics, pose, eye blink, breath,
  gaze, user data, hit areas, and deterministic update ordering.
- [ ] **L2D8 — emotion and body-language policy:** map application-level intent
  to authored motion/expression combinations through a data-driven controller.
- [ ] **L2D9 — speech integration:** lip-sync inputs and optional TTS/audio
  coupling through narrow interfaces; Live2D does not depend on a specific TTS
  provider.
- [ ] **L2D10 — engine/editor integration:** Gameplay component, scene
  composition, inspector, reimport/hot reload, packaging, and command/capture
  support.
- [ ] **L2D11 — measured performance:** profile first, then consider async
  streaming, shared mask atlases, bindless batching, update-rate decoupling, or
  Sakura-style CPU-visible VRAM paths.

## Open decisions before L2D3

- [ ] Request and record the stable Live2D type value through the Asset-owned
  type registry; do not add a Live2D-specific type branch to Asset.
- [ ] Decide whether V1 publishes to an explicit authored output path or uses a
  new generic content-addressed archive API. Do not couple Live2D to the current
  model-specific database internals.
- [ ] Approve the runtime/import test model and document its redistribution
  terms before checking it into the repository.
- [ ] Freeze the native `.live2d` required/optional feature-bit policy,
  including how Cubism 5.3 offscreen drawing is rejected or represented.
- [ ] Freeze texture alpha/color-space handling from an official R5 reference
  capture before shader implementation.
