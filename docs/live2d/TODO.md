# Live2D Module Roadmap

**Status: active.** Architecture and reference analysis live in
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
- [x] **L2D3 — offline Live2D import command:** connect the module-owned
  `.model3.json` provider to `KimPeanutAssetTool import-live2d`, publish the
  deterministic `.live2d` root and native Texture dependencies beside the
  requested output, and reject immutable product collisions without exposing a
  partial root.
- [x] **L2D4 — Live2D planning and generic Render submission**
  ([concrete plan](.plan/L2D4.md)): six gated subtasks—
  [x] [contract freeze](.plan/L2D4.0.md) ([journal](../../.spec/journal/2026-09-09-live2d-l2d4-0.md)),
  [x] [common streaming geometry](.plan/L2D4.1.md) ([journal](../../.spec/journal/2026-09-10-live2d-l2d4-1-rhi-fixes.md)),
  [x] [SDK-free extraction](.plan/L2D4.2.md) ([journal](../../.spec/journal/2026-09-10-live2d-l2d4-2.md)),
  [x] [generic submission and unmasked planning](.plan/L2D4.3.md) ([journal](../../.spec/journal/2026-09-10-live2d-l2d4-3.md)),
  [x] [packed masks as generic passes](.plan/L2D4.4.md) ([journal](../../.spec/journal/2026-09-11-live2d-l2d4-4.md)), and
  [x] [cross-backend hardening](.plan/L2D4.5.md) ([journal](../../.spec/journal/2026-09-11-live2d-l2d4-5.md)).
  L2D4 ends with an offscreen result produced through a semantic-free Render
  executor; both backends now agree inside the frozen tolerances on the
  cross-backend capture gate, so the window, presentation, and final capture
  workflow move to L2D5. Three L2D4.5 residuals are carried into L2D5: target
  resize is the one hardening-matrix scenario with **no evidence at all** (no
  test calls `Live2DRenderer::ResizeOutput` and no run resized the target, so
  its transactional claim rests on code reading); shutdown handle accounting
  exits cleanly four times over but is not asserted by a test; and
  `CaptureView::Live2D` still names a module inside the generic capture enum.
- [ ] **L2D5 — dedicated viewer and V1 evidence**
  ([concrete plan](.plan/L2D5.md)): execute five ordered subtasks—
  [x] [evidence contract freeze](.plan/L2D5.0.md)
  ([journal](../../.spec/journal/2026-09-12-live2d-l2d5-0.md)),
  [x] [generic host capture view](.plan/L2D5.1.md)
  ([journal](../../.spec/journal/2026-09-12-live2d-l2d5-1.md)),
  [x] [resize and shutdown runtime evidence](.plan/L2D5.2.md)
  ([journal](../../.spec/journal/2026-09-12-live2d-l2d5-2.md)),
  [x] [fixture, blend coverage, and reference disposition](.plan/L2D5.3.md)
  ([journal](../../.spec/journal/2026-09-12-live2d-l2d5-3.md)), and
  [ ] [closeout and L2D6 handoff](.plan/L2D5.4.md). The standalone viewer itself
  landed early as an `IApplicationHost` host mode of `KimPeanutEngine.exe`
  (commits `6219e50`, `d8e9187`), **not** as the separate
  `KimPeanutLive2DViewer` binary this item originally named; the plan text is
  corrected in L2D5.4. The one V1 defect (`CaptureView::Live2D` in Render's
  capture enum), the resize evidence, and the asserted shutdown accounting are
  all closed by L2D5.1 and L2D5.2. L2D5.3 closed item 9 from fixture data on
  both backends (the SDK's Mao: 239 normal / 15 additive / 8 multiplicative of
  262 drawables) and recorded item 11 as an accepted limitation, blocked
  because the official sample needs GLEW 2.2.0, which the SDK does not ship and
  this environment cannot fetch. Two things carry into L2D5.4: the cross-backend
  tolerance miss at the resized extent L2D5.2 recorded, reproduced exactly by
  L2D5.3, and a **new open defect** — Mao renders deterministically differently
  on OpenGL and Vulkan (0.40% of pixels, RGB-only, face-localized) at both
  extents, so Mao cannot serve as a cross-backend fixture until it is resolved.

## V1 acceptance

- [x] Live2D is enabled by default for development, while
  `KPENGINE_ENABLE_LIVE2D=OFF` allows the ordinary engine to configure/build
  without a Cubism SDK installation.
  *Verified (2026-09-12):* `option(... ON)` at `engine/CMakeLists.txt:1`; a
  `build-nolive2d` configure and build with the option `OFF` completes with no
  Cubism SDK path set.
- [x] Enabling Live2D with a missing, mixed-version, or unsupported Core package
  fails during configuration or initialization with a precise diagnostic.
  *Verified (2026-09-12):* `cmake/FindLive2DCubism.cmake` raises `FATAL_ERROR`
  for a missing root, each missing required file, and a `cubism-info.yml`
  release other than `5-r.5` (`KPENGINE_CUBISM_SDK_VERSION STREQUAL "5-r.5"`,
  line 91), so a mixed or unsupported package fails at configure time naming the
  mismatch rather than at first use.
- [x] Asset and AssetImport contain no Live2D include, type declaration, loader
  branch, or source-suffix branch.
  *Verified (2026-09-12):* `grep -ri live2d engine/runtime/asset/` returns
  **0** matches.
- [x] Live2D uses the Asset-owned polymorphic payload and registration path;
  Asset contains no Live2D name, include, payload, suffix, or loader branch.
  *Verified (2026-09-12):* same 0-match grep; the module registers custom type
  `0x1000` from its own side.
- [x] Live2D integration tests cover payload type agreement, native loader
  registration, texture dependency handling, rollback, and unloading through
  ordinary AssetManager transactions.
  *Verified (2026-09-12):*
  `Live2DAssetTest.LoadsImportedProductThroughAssetManagerDependencies` (loader
  registration, texture dependencies, `UnRegisterAsset` leaving live count 0
  while existing instances stay valid),
  `Live2DAssetTest.ImportsCheckedInModel3PackageDeterministically`, and
  `Live2DCoreTest.RejectsShutdownWhileModelLeasesAreAlive`.
- [x] Import rejects path escape, malformed JSON, missing `.moc3`/texture data,
  unsupported required features, and corrupt products without publishing a
  partial root.
  *Implemented and mostly verified (2026-09-12):* all five rejections exist in
  `live2d_import.cpp` (missing/oversized source line 76, missing Moc or
  Textures line 223, invalid Moc line 236, duplicate texture line 270). Tests
  cover path escape and non-model JSON
  (`RejectsSourcePathEscapeAndGenericJson`), unsupported features
  (`Live2DModelDataContractTest.RejectsUnsupportedFeatureReport`), and a
  corrupted product with the live-asset count still 0
  (`live2d_asset_test.cpp:246-256`). The missing-`.moc3` and invalid-Moc
  branches now have direct coverage:
  `Live2DAssetTest.RejectsMissingAndInvalidMocReferences` drives four cases
  (absent Moc key, empty Textures, a Moc that is not a `.moc3`, and an absent
  Moc file) and asserts both `result.product == nullptr` and the specific
  diagnostic, so a rejection that publishes nothing but names the wrong reason
  fails too ([L2D5.2](../../.spec/journal/2026-09-12-live2d-l2d5-2.md)).
- [x] One native `.live2d` asset can create at least two independent model
  instances that share immutable data and textures but not parameter state.
  *Verified (2026-09-12):* `Live2DCoreTest.CreatesIndependentModelsFromOneMoc`
  runs and passes (11 ms — confirmed with a direct
  `--gtest_filter` run, because this test contains a `GTEST_SKIP` branch that
  would otherwise report as a pass). `live2d_asset_test.cpp:225-230` also
  asserts the first instance's parameter change while the second's value is
  unchanged.
- [x] Live2D emits an ordered generic `render::RenderSubmission`; Render and
  Graphics contain no Live2D include, semantic enum, pass ID, or dispatch branch.
  *Verified (2026-09-12):* the module name was retired from Render's capture
  enum by
  [L2D5.1](.plan/L2D5.1.md) ([journal](../../.spec/journal/2026-09-12-live2d-l2d5-1.md)):
  `CaptureView::Live2D` became the role-named `CaptureView::HostOutput`, and the
  deferred renderer now classifies views with the positive
  `RequiresCaptureViewConversionPass` instead of excluding names.
  `grep -rni live2d engine/runtime/render/ engine/runtime/graphics/` returns
  **0** matches. The classification is pinned per enumerator by
  `RenderCaptureServiceTest.ClassifiesEveryCaptureViewExplicitly`, and the
  runtime end-to-end capture has since been re-confirmed on both backends
  ([L2D5.2 journal](../../.spec/journal/2026-09-12-live2d-l2d5-2.md)).
  **Residual:** the `"live2d"` CLI string survives deliberately in generic
  runtime tooling as a launch-time alias for `host_output`.
- [ ] One representative clipped model renders with correct order, opacity,
  normal/add/multiply blending, masks, canvas transform, and transparent
  background on OpenGL and Vulkan. *Cross-backend gate green (2026-09-11):*
  the configured clipped Hiyori product renders on both APIs and every compared
  region — non-edge opaque, partial alpha, transparent background, filtered
  edge — is inside the frozen tolerances, with two mask contexts active. The
  comparison does not yet assert that all three blend modes appear in the
  fixture, so additive/multiplicative coverage is not separately proven.
- [x] The dedicated viewer resizes, captures a stable frame, and exits without
  Graphics validation errors, leaked Cubism objects, or live GPU handles.
  *Verified (2026-09-12):*
  [L2D5.2](../../.spec/journal/2026-09-12-live2d-l2d5-2.md) added the `--resize
  WIDTHxHEIGHT` launch option and fixed a real defect it exposed —
  `WindowSystem::SetWindowSize` only cached a size and never called
  `glfwSetWindowSize`, so no programmatic resize could reach a Vulkan surface.
  Both backends then resized to 1024×768, exported a 1024×768 capture, logged 0
  error/warning/validation/leak lines, reached
  `CubismFramework::Dispose() is complete.`, and exited 0; repeating a run
  reproduces the file byte for byte on both backends. The shutdown counts are
  now asserted rather than read from a log:
  `Live2DRendererTest.CleanupReleasesEveryHandleAndModelInstance` requires
  `GetLiveGpuHandleCount() == 0`, `LiveModelInstanceCount() == 0`, and the fake
  backend's destroy counts to match its create counts, and
  `CubismLifecycle::Shutdown` refuses while a model lease is live.
  `Live2DRendererTest` also covers all four transactional `ResizeOutput` cases
  (swap-after-`WaitIdle`, failed-create leaves the target and view intact, zero
  extent rejected untouched, same extent a no-op), each confirmed to fail when
  its defect is injected. **Caveat:** at 1024×768 cross-backend
  `non-edge opaque` agreement exceeds the frozen tolerance (81 of 786432 pixels,
  `max_abs` 3, on internal clip/mask boundaries the comparator's edge mask cannot
  classify). Item 10's own criterion is met; pixel agreement at the resized
  extent is L2D5.3's to disposition.
- [ ] Visual evidence is compared against the official R5 renderer with
  documented tolerances; compilation alone is not accepted.
  *Blocked:* the licensed external fixture has no built official sample
  executable, so no official-reference image exists to compare against.
  Cross-backend agreement (OpenGL vs Vulkan) is not a substitute for this and
  is not recorded as one.

## Post-V1 roadmap

- [ ] **L2D6 — authored motion and expression playback**
  ([concrete plan](.plan/L2D6.md),
  [execution spec](../../.spec/specs/live2d-authored-playback.md)): execute five
  ordered subtasks—
  [x] [contract freeze](.plan/L2D6.0.md)
  ([journal](../../.spec/journal/2026-09-12-live2d-l2d6-0.md)),
  [x] [typed product/import](.plan/L2D6.1.md) ([journal](../../.spec/journal/2026-09-12-live2d-l2d6-1.md)),
  [ ] [instance-local clip library](.plan/L2D6.2.md),
  [ ] [deterministic playback transaction](.plan/L2D6.3.md), and
  [ ] [hardening/L2D7 handoff](.plan/L2D6.4.md). L2D6 exposes named motion groups,
  one logical expression channel, fades, priorities, cancellation, completion,
  and value-owned events without embedding selection policy in the shared asset.
  L2D6.0 froze 42 decisions against pinned R5 source and a standalone probe, and
  demonstrated that V1 cannot import any `model3.json` containing `Expressions`
  and silently discards numeric fade overrides, so V1 playback must require
  reimport rather than reading `optional_chunks`. L2D6.0's own gate is closed.
  Two of the three items that blocked the parent plan's L2D5 entry gate are now
  cleared — `CaptureView::Live2D` was retired by L2D5.1 and resize plus shutdown
  now have runtime evidence from L2D5.2 — so only the official-reference
  comparison remains blocked, along with L2D5.3's fixture and blend coverage and
  L2D5.2's recorded tolerance miss at the resized extent. That reconciliation
  must happen before L2D6.4.
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

## L2D4 planning decisions and remaining gates

- [x] Live2D owns registered custom type value `0x1000`; Asset contains no
  Live2D-specific type branch.
- [x] V1 uses an explicit `.live2d` output path with a sibling `.archive`
  directory; it does not couple Live2D to the current model-specific database
  internals.
- [ ] Approve the runtime/import test model and document its redistribution
  terms before checking it into the repository.
- [x] L2D4 V1 rejects Cubism 5.3 offscreen/blend groups during static model
  extraction before GPU publication; support requires a later planned stage.
- [x] Freeze texture alpha/color-space and PMA equations from the pinned R5
  source in L2D4.0. Official image comparison remains an L2D5 evidence gate
  because the licensed external fixture has no built official sample executable.
