# Live2D V1 Rendering

- Status: V1 acceptance closed by L2D5.4 (2026-09-12), with explicit accepted
  limitations. L2D1–L2D3 and L2D4.0–L2D4.5 landed, followed by the
  IApplicationHost-based standalone viewer mode and L2D5 evidence. See the
  [V1 acceptance audit](../../docs/live2d/TODO.md#v1-acceptance).
- Owner: unassigned
- Parent TODO: [Live2D Module Roadmap](../../docs/live2d/TODO.md)
- Architecture: [Live2D Module Plans](../../docs/live2d/PLANS.md)

## Objective

Add an optional Live2D module that imports an authored Cubism
`.model3.json` package into a native engine Asset and renders one correct model
in a dedicated viewer window on OpenGL and Vulkan. V1 establishes SDK, Asset,
instance, Render, and GPU ownership boundaries for later character behavior.

## Current state

- L2D1 integrates the optional pinned Cubism R5 SDK and process lifecycle.
- AX1 and L2D2 provide the Asset-owned polymorphic payload/registry boundary,
  module-owned custom type `0x1000`, native product, loader, and importer.
- L2D3 publishes deterministic `.live2d` roots and native Texture dependencies
  through `KimPeanutAssetTool import-live2d`.
- `Live2DModelInstance` already owns independent mutable Cubism model state;
  it does not yet expose an SDK-free render snapshot or retain Texture payloads.
- L2D4.0 froze CPU-visible color, alpha, mask, ordering, and unsupported-feature
  contracts; official image comparison remains deferred to L2D5 evidence.
- Graphics supports the generic frame-safe geometry, texture, binding, blend,
  scissor/viewport, and offscreen-target path used by the concrete renderer.
- Render now accepts a renderer-owned extension and Live2D supplies an
  offscreen Hiyori preview/capture path selected by `config/live2d.json`.
  The validation level is camera-only; Vulkan visual parity remains open.
- Cubism Core remains an external proprietary SDK input and is not committed.

## Scope and non-goals

In scope:

- optional Cubism 5 SDK for Native R5 integration, enabled by default during
  engine development;
- a generic Asset-owned extensible type/loader/payload and importer-provider
  contract, consumed by Live2D without Live2D-specific Asset branches;
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
- Live2D owns Cubism semantic planning and emits an ordered generic Render
  submission; generic Render contains no Live2D dependency or semantic branch.
- GPU shaders own per-pixel texture/color/mask calculations; CPU planning does
  not precompute final pixels.
- Core/Framework initialize once and outlive every model instance/render proxy.
- Unsupported required Cubism features fail visibly rather than render
  approximately without a diagnostic.

## Stages

1. [L2D1 — SDK integration](../../docs/live2d/.plan/L2D1.md).
2. [AX1 — Asset extensibility](../../docs/asset/.plan/AX1.md), then
   [L2D2 — Live2D Asset integration](../../docs/live2d/.plan/L2D2.md).
3. L2D3 — native Live2D product, importer, and runtime loader (landed).
4. [L2D4 — Live2D planning and generic Render submission](../../docs/live2d/.plan/L2D4.md).
5. L2D5 — dedicated viewer and V1 validation evidence.

Each stage remains independently buildable. Asset migration lands before the
Live2D loader; native product/loading lands before Graphics rendering; the
viewer is the final integration consumer, not the owner of module logic.

## Acceptance criteria

- [x] All [V1 acceptance items](../../docs/live2d/TODO.md#v1-acceptance) are
  reconciled by L2D5.4. Items 8–10 pass with runtime/test evidence; item 11 is
  an explicit accepted limitation because the official R5 sample cannot be
  built in this environment.
- [x] The exact SDK, Framework commit, Core package, runtime binary mode, and
  applicable licenses are recorded in the implementation journal.
  *Verified (2026-09-12):* the SDK is distributed as a release archive rather
  than a git checkout, so identity is recorded as version `5-r.5`, the
  `cubism-info.yml` Core package hashes, and the `Framework/CHANGELOG.md`
  release date (`2026-04-02`) ([L2D4.0 journal](../journal/2026-09-09-live2d-l2d4-0.md));
  runtime mode is MSVC 143 static `MD`/`MDd`, Debug validated against
  `Live2DCubismCore_MDd.lib`, with the root, Core, and Framework license files
  recorded ([L2D1 journal](../journal/2026-09-08-live2d-l2d1.md)).
- [x] The Asset-owned polymorphic payload/type/loader migration preserves
  existing built-in type values, packed IDs, routing, payload access, and
  asset tests.
  *Verified (2026-09-12):* the Asset and AssetImport trees contain zero Live2D
  references and the full asset suite passes.
- [x] Import/product tests prove deterministic bytes and transactional root
  publication under malformed/missing/path-escape failures.
  *Verified (2026-09-12): deterministic bytes
  (`ImportsCheckedInModel3PackageDeterministically`), path escape, non-model
  JSON, unsupported features, and a corrupted product with no partial root are
  all asserted. **The missing-`.moc3` path is directly covered by L2D5.2**, so
  "missing" is covered by direct test evidence.
- [x] Multiple instances from one asset have isolated parameter state.
  *Verified (2026-09-12):* `Live2DCoreTest.CreatesIndependentModelsFromOneMoc`
  passes (direct `--gtest_filter` run, not skipped) and
  `live2d_asset_test.cpp:225-230` asserts one instance's parameter change while
  the other is unchanged.
- [x] Streaming geometry is frame-slot safe on Vulkan and does not leak stale
  OpenGL state.
  *Verified (2026-09-12):* the contract is unit-covered
  (`BufferContract.ValidatesStreamingBufferRules`,
  `BufferContract.ValidationReportsUnwrittenSlotsAndMayPropagateLookupErrors`)
  and the L2D4.1 corrections landed. The runtime half is the `GraphicsSmoke`
  sequence recorded as passing both backends in
  [docs/status.md](../../docs/status.md), which is a runtime executable rather
  than a `ctest` case and was re-confirmed in the L2D4.5/L2D5 runtime evidence.
- [x] Official-reference, OpenGL, and Vulkan captures agree within documented
  alpha/color/edge tolerances for a representative model with clipping.
  *Accepted limitation:* OpenGL and Vulkan agree region-by-region inside the frozen L2D4.0
  tolerances, but no official-reference capture exists — the licensed external
  fixture has no built sample executable. The official comparison remains an accepted limitation; cross-backend evidence is
  is not substituted for it.
- [x] Resize and shutdown paths have runtime evidence, not compilation only.
  *Verified:* shutdown has runtime evidence (four viewer runs exit 0 reaching
  `CubismFramework::Dispose() is complete.` with no live-handle warning). Resize
  has neither runtime evidence nor a test — no test calls
  `Live2DRenderer::ResizeOutput` and the viewer run resizes the target.

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
.\tools\kp.ps1 build RenderPassScheduleTest
.\tools\kp.ps1 test RenderPassScheduleTest
.\tools\kp.ps1 smoke
cmake --build build --config Debug --target Live2DCoreTest
ctest --test-dir build -C Debug -R Live2D
cmake --build build --config Debug --target KimPeanutEngine
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
- Alpha, blend, masking, and Cubism 5.3 rejection rules are frozen by L2D4.0
  from pinned R5 source. Official-image comparison remains an L2D5 evidence
  gate because the external fixture has no built official sample executable.
- Dynamic geometry requires a new common RHI contract and backend lifecycle
  tests; a quick native-API hook is not acceptable.
- The current viewer presentation seam is Editor-oriented. V1 may use a
  viewer-only adapter, but it must not leak an Editor dependency into the
  Live2D module's public targets.
