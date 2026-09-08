# Live2D V1 Rendering

- Status: proposed
- Owner: unassigned
- Parent TODO: [Live2D Module Roadmap](../../docs/live2d/TODO.md)
- Architecture: [Live2D Module Plans](../../docs/live2d/PLANS.md)

## Objective

Add an optional Live2D module that imports an authored Cubism
`.model3.json` package into a native engine Asset and renders one correct model
in a dedicated viewer window on OpenGL and Vulkan. V1 establishes SDK, Asset,
instance, Render, and GPU ownership boundaries for later character behavior.

## Current state

- TTS demonstrates an optional static module but has no asset/render burden.
- Asset type, payload, extension mapping, loader dispatch, and importer-tool
  dispatch are closed over built-in types.
- Graphics supports static indexed mesh drawing, textures, bindings, blend
  state, scissor/viewport, and offscreen targets, but not generic frame-safe
  streaming geometry.
- Render has a fixed schedule and API-neutral backend but no Cubism-backed
  Live2D source, pass, or viewer composition; the editor currently has only a
  placeholder preview panel.
- Cubism SDK is not present in the repository and Cubism Core is proprietary.

## Scope and non-goals

In scope:

- optional Cubism 5 SDK for Native R5 integration, enabled by default during
  engine development;
- explicit extensible Asset type/loader/payload and importer-provider
  contracts;
- deterministic native `.live2d` product and offline importer;
- immutable model Asset plus independent mutable model instances;
- custom API-neutral Live2D renderer with correct still-frame drawable,
  blend, and mask behavior;
- standalone viewer, resize, capture, cross-backend and official-reference
  evidence.

Out of scope:

- emotion or body-language policy;
- motion/expression playback API, physics, pose, gaze, eye blink, hit areas,
  user-data behavior, or motion sync;
- TTS/lip-sync integration;
- Gameplay/Editor authoring components and production packaging;
- render graph, DirectStorage, bindless batching, or speculative performance
  redesign.

## Invariants

- Explicitly disabling Live2D leaves the existing engine build and runtime
  behavior unchanged and requires no Cubism SDK.
- Asset never names or includes Live2D; Live2D depends downward on Asset.
- Runtime load consumes native products and never imports/mutates source.
- Shared Asset payload is immutable; per-character Cubism state is not shared.
- GPU objects and synchronization remain owned by Graphics/RHI.
- Common contracts expose no native OpenGL/Vulkan or proprietary Core types.
- Core/Framework initialize once and outlive every model instance/render proxy.
- Unsupported required Cubism features fail visibly rather than render
  approximately without a diagnostic.

## Stages

1. [L2D1 — SDK integration](../../docs/live2d/.plan/L2D1.md).
2. L2D2 — extensible Asset and importer contracts.
3. L2D3 — native Live2D product, importer, and runtime loader.
4. L2D4 — mutable instance model and common RHI renderer.
5. L2D5 — dedicated viewer and V1 validation evidence.

Each stage remains independently buildable. Asset migration lands before the
Live2D loader; native product/loading lands before Graphics rendering; the
viewer is the final integration consumer, not the owner of module logic.

## Acceptance criteria

- [ ] All [V1 acceptance items](../../docs/live2d/TODO.md#v1-acceptance) pass.
- [ ] The exact SDK, Framework commit, Core package, runtime binary mode, and
  applicable licenses are recorded in the implementation journal.
- [ ] Asset registry migration preserves all existing built-in asset tests.
- [ ] Import/product tests prove deterministic bytes and transactional root
  publication under malformed/missing/path-escape failures.
- [ ] Multiple instances from one asset have isolated parameter state.
- [ ] Streaming geometry is frame-slot safe on Vulkan and does not leak stale
  OpenGL state.
- [ ] Official-reference, OpenGL, and Vulkan captures agree within documented
  alpha/color/edge tolerances for a representative model with clipping.
- [ ] Resize and shutdown paths have runtime evidence, not compilation only.

## Validation plan

Minimum final validation level is 4 because V1 changes public Asset and
Graphics contracts, CMake target dependencies, persistent product bytes, and
runtime rendering.

Expected evidence includes:

```powershell
.\tools\kp.ps1 build AssetUnitTest
.\tools\kp.ps1 test Asset
.\tools\kp.ps1 build GraphicsContractTest
.\tools\kp.ps1 test GraphicsContractTest
.\tools\kp.ps1 smoke
cmake --build build --config Debug --target Live2DUnitTest
ctest --test-dir build -C Debug -R Live2D
cmake --build build --config Debug --target KimPeanutLive2DViewer
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

Run the viewer separately for OpenGL and Vulkan with the same native asset,
capture stable frames, inspect them visually, and compare them to the official
R5 sample render. Record any test skipped because the proprietary SDK or
licensed model fixture is unavailable.

## Risks and open questions

- Cubism Core redistribution and test-model licenses may constrain CI and
  checked-in fixtures.
- The current model-specific archive database is not yet a generic product
  repository; V1 must choose explicit output or generalize that boundary
  without a Live2D-to-model-import dependency.
- Correct alpha, blend, masking, and Cubism 5.3 offscreen behavior need an
  official-reference capture before shader contracts are frozen.
- Dynamic geometry requires a new common RHI contract and backend lifecycle
  tests; a quick native-API hook is not acceptable.
- The current viewer presentation seam is Editor-oriented. V1 may use a
  viewer-only adapter, but it must not leak an Editor dependency into the
  Live2D module's public targets.
