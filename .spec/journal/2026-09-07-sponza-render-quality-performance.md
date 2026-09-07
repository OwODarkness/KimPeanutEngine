# 2026-09-07 — Sponza Render Quality and Performance

**Status: Stage 0 instrumentation landed; native baseline evidence pending.**

Links: [issue](../../docs/render/issue/issue-9.7.md),
[review](../../docs/render/.review/issue-9.7.md),
[plan](../../docs/render/.plan/issue-9.7.md), and
[spec](../specs/sponza-render-quality-performance.md).

## Investigation checkpoint

- Inspected scene-color, base-color, world-normal, material-parameter, and
  shadow-visibility captures. Speckle is already present in sampled G-buffer
  material channels and is not spatially explained by shadow visibility.
- Traced texture creation to one mip level and sampler LOD zero. Confirmed the
  OpenGL mip-generation branch and that Vulkan's current upload copies only
  level zero.
- Parsed the exact selected glTF dependency closure: 115 meshes, 405
  primitives, 28 materials, and 72 referenced images; all referenced images are
  4096×4096. Primitive alpha modes are 401 opaque and 4 blend.
- Calculated approximately 1.89 GiB compressed source bytes, 4.5 GiB decoded
  RGBA8 base levels, and 6 GiB for complete uncompressed mip chains.
- Traced Vulkan material bindings to a pool-per-set lifecycle and G-buffer
  submission to roughly 401 transient material sets per frame.
- Traced visibility to one whole-model proxy bound and directional shadows to
  all-section redraw every frame, for roughly 806 geometry draws before
  fullscreen passes.
- Reviewed the linked Sponza frame analysis and source implementation. Adopted
  its applicable per-mesh culling and measured depth-prepass lessons; rejected
  its hardware/resolution result as a project performance baseline.
- Reviewed Khronos' descriptor-management sample as supporting evidence for
  pool reuse/caching, then mapped lifetime to this engine's frame slots and
  fences.

## Changes in the investigation checkpoint

The initial checkpoint added the formal review, stage design, execution spec,
issue entry, roadmap links, and project-status entry. The Stage 0 checkpoint
below adds the first runtime instrumentation slice.

## Validation

- Read-only source and manifest inspection completed.
- Relative Markdown link scan passed for all changed records.
- Trailing-whitespace scan passed for all changed records.
- `git diff --check` passed for tracked documentation changes. New records were
  covered by the explicit trailing-whitespace scan.
- Native CMake build and runtime capture are not claimed for this checkpoint;
  the build is blocked before compilation by the machine's Windows SDK probe.

## Remaining evidence and risk

- CPU/GPU/present timing attribution is implemented but has not yet been
  collected from the native runtime.
- Device-reported resident memory, format support, and descriptor counts are
  instrumented but have not yet been captured.
- Current diagnostic captures are static images; camera-motion stability needs
  a reproducible path or sampled poses.
- The proposed memory ceiling and 16.67 ms target require measurement on the
  recorded reference machine; neither is a current result.
- All seven review findings remain open.

## Stage 0 implementation checkpoint

- Added the checked-in `sponza-stage-0` scenario descriptor: `level/sponza.level`,
  `main_camera`, 1920×1080, Vulkan as the initial API, Debug build, 120 warm-up
  frames, and a 300-frame sample window.
- Added a Render-owned value profile snapshot with CPU phase timings, per-pass
  CPU timings, draw/section counts, descriptor set/pool counts, present mode,
  shadow baseline counters, and texture source/decoded/resident bytes.
- Added common backend-neutral profiling hooks and native timer-query
  implementations for Vulkan timestamp queries and OpenGL timer queries.
  GPU timings are consumed only after the backend's safe frame completion
  point; unavailable timer support leaves CPU telemetry active.
- Added prepared-catalog texture closure accounting and resolver resident-byte
  accounting. The closure count is captured before renderer-owned built-ins so
  the Sponza dependency count remains attributable to the selected level.
- Added Render profile contract tests and surfaced the key Stage 0 readouts in
  the Editor profile bar. The profile window also emits one structured
  completion log with p50/p95 CPU/present values and the measured counters;
  OpenGL samples are finalized after the external `SwapBuffers()` timing is
  recorded.

## Stage 0 validation

- `git diff --check` — passed.
- MinGW C++17 syntax checks — passed for Render, Runtime, Editor, Vulkan, OpenGL,
  and `render_profile_test.cpp`.
- `cmake --build build --config Debug --target Render Graphics
  RenderPassScheduleTest RenderSystemTest` — blocked before compilation by
  MSBuild's denied access to `C:\Users\17519\AppData\Local\Microsoft SDKs`
  while evaluating the Windows SDK probe.
- Vulkan/OpenGL runtime baseline samples and image captures — not claimed;
  they require the blocked native build and a controllable GLFW window.

## Stage 1 runtime mip checkpoint

- Added `TextureSemantic` metadata and explicit level-one-through-N
  `TextureMipSubresource` payloads while retaining level-zero `pixels` for
  existing CPU consumers.
- Added the bounded loose-texture mip generator. LDR color mips filter in
  linear space before sRGB encoding, normal mips average tangent-space
  vectors and renormalize, packed channels remain linear, and opacity masks
  retain covered texels at the cutout threshold. The fallback caps the base
  dimension at 2048 until native profile-specific texture products exist.
- Added filename classification for transitional loose textures. Native
  archive products still need to carry authoritative semantic metadata rather
  than relying on this fallback.
- Changed the common texture manager to derive the image mip count from the
  initialized payload and reject malformed chains. Vulkan packs all supplied
  levels into one staging upload with one `VkBufferImageCopy` region per mip;
  OpenGL uploads each level explicitly and no longer relies on implicit
  driver generation. Sampler LOD bounds now permit the populated chain.
- Updated decoded/resident texture accounting to include all uploaded levels.
- Added deterministic tests for bounded chains, sRGB color filtering, normal
  renormalization, packed linear averages, opacity coverage, and semantic
  classification.

## Stage 1 validation

- `cmake --build build --config Debug --target RenderPassScheduleTest` — passed.
- `RenderPassScheduleTest.exe` — 98/98 tests passed.
- `ctest --test-dir build -C Debug -R "TextureMipmaps" --output-on-failure` —
  5/5 tests passed.
- MinGW C++17 syntax checks for the changed data, Asset, Render, OpenGL, and
  Vulkan translation units — passed.
- GitHub MCP reference discovery — unavailable in this session; existing
  local engine-reference studies and the repository's Vulkan/OpenGL contracts
  were used instead.

## Stage 1 remaining evidence

- Native archive texture products with serialized semantic mip artifacts are
  not yet implemented; the current path is the bounded runtime fallback.
- The existing `GraphicsSmoke` executable built and exercised both Vulkan and
  OpenGL with the mip-backed textures. Its D5 cross-backend silhouette policy
  failed at `raw=375`, `structural=82`, although the captures were visually
  aligned; this remains an image-comparison follow-up, not a claimed smoke
  pass.
- The initial profile-query poll was also hardened to skip collection until a
  frame submission has completed, avoiding an uninitialized first-frame query
  read.
- Sponza runtime image captures and measured memory/LOD behavior still require
  the controllable Runtime host and remain unclaimed.

## Stage 2 native texture cook checkpoint

- Added `NativeTexture` V1, a canonical little-endian `.texture` product with
  semantic metadata, explicit level directories, integrity digest, bounded
  dimensions, and complete payload-range validation before allocation.
- Added database-free `TextureImporter` and `TextureCooker` stages. The
  importer decodes source files through ImageIO; the cooker converts RGBA32F
  HDR data to RGBA16F, selects portable RGBA8/RGBA16F storage, applies the
  bounded semantic mip policy, and serializes native bytes. No AssetManager,
  Runtime, Render, Graphics, or GPU object is involved.
- Updated native model/material import so embedded and external material images
  become content-addressed `.archive/textures/<hash>.texture` products.
  Runtime `NativeTextureLoader` consumes those products read-only; loose image
  loading remains as a migration fallback for authored sources.
- Added `KimPeanutAssetTool cook-texture` for direct source cooking and tests
  for importer/cooker independence, native round-trip mips, and explicit
  rejection of unavailable block compression.

## Stage 2 validation

- `cmake --build build --config Debug --target TextureImportTest` — passed.
- `cmake --build build --config Debug --target AssetRuntime` — passed.
- `cmake --build build --config Debug --target NativeMaterialTest ModelImportServiceTest` — passed.
- `ctest --test-dir build -C Debug -R "(NativeMaterialTest|ModelImportServiceTest|TextureImportTest)" --output-on-failure` — 13/13 passed.
- The current common `TextureFormat` contract has no BCn/ASTC formats, so
  `RequireBlockCompression` fails explicitly and portable products remain the
  only supported cooked profile. Cross-backend Sponza capture and resident-byte
  ceiling evidence remain pending.

## Stage 3 descriptor-pool lifetime checkpoint

- Replaced Vulkan's pool-per-resource-binding-set path with private descriptor
  arenas owned by backend frame slots. The initial arena is sized for the
  expected steady-state material workload; a full arena grows by appending a
  larger arena for that slot, which is retained and reused on later cycles.
- `VulkanBackend::BeginFrame` resets only the current slot's arenas immediately
  after its in-flight fence wait. Transient descriptor handles from that slot
  are invalidated before `FrameContext` releases its previous list, and set
  destruction no longer calls `vkDestroyDescriptorPool`.
- Corrected Stage 0 descriptor telemetry so `descriptor_pools_created` counts
  actual arena creation rather than every descriptor set creation.

## Stage 3 validation

- `cmake --build build --config Debug --target GraphicsSmoke` — passed.
- `GraphicsSmoke.exe` — reached the existing D5 cross-backend silhouette
  comparator failure, but no descriptor lookup errors or Vulkan missing-set
  validation errors were emitted after the arena change.
- Stable material-binding caching and dedicated pool lifecycle/growth tests —
  not yet implemented; the current checkpoint fixes the pool lifetime/churn
  defect only.

## Stage 4 visibility and static-shadow checkpoint

- Extended `data::MeshSection` with a mesh-local AABB. Native model version 2
  serializes six section-bound floats per section and validates finite,
  non-inverted bounds plus containment of every indexed vertex. Version 1
  products remain readable by deriving section bounds from their existing index
  ranges, which preserves checked-in archives during migration.
- Added a Render-owned section visibility helper. It first rejects invisible or
  out-of-frustum proxies, then transforms each section AABB into world space
  and rejects individual sections. Missing/invalid bounds remain conservative.
  G-buffer, directional, spot, and point shadow recording now use the resulting
  section identity and issue one indexed draw for each retained section.
- Added a conservative directional shadow validity stamp covering light and
  shadow identity, camera fit position, caster-section identity, transforms,
  materials, and world bounds. A matching valid stamp skips the directional
  target recording; resize, disabled-target clearing, or any stamp input change
  forces redraw. This keeps correctness ahead of aggressive cache assumptions.

## Stage 4 validation

- `cmake --build build --config Debug --target AssetUnitTest RenderPassScheduleTest` — passed.
- `AssetUnitTest.exe --gtest_filter=LevelLoaderTest.LoadsCheckedInGameplayLevelFixtures` — passed; legacy version-1 model products remained readable.
- `RenderPassScheduleTest.exe` — 98/98 tests passed.
- `cmake --build build --config Debug --target GraphicsSmoke` — passed.
- `GraphicsSmoke.exe` — reached the existing D5 Vulkan/OpenGL silhouette
  comparator failure (`raw=375`, `structural=82`, `area_delta=135`, bounds
  match). No new Vulkan descriptor/set errors were emitted; this remains an
  existing cross-backend validation blocker rather than a Stage 4 compile or
  initialization failure.
- Full asset suite and fresh Sponza capture remain pending; runtime smoke still
  uses the checked-in legacy/native mix, so section-cull draw-count evidence
  should be captured with the controllable Runtime host in the next checkpoint.

## Stage 5 measured pass optimization checkpoint

- Added a Render-owned `SortOpaqueFrontToBack` policy for opaque G-buffer
  sections. It orders section world-bound centers by camera depth and uses the
  existing pipeline/material/mesh/section key as a deterministic tie-break.
  Alpha-blend lists and shadow ordering are unchanged.
- Hardened the profile hook so its fixed Sponza window does not consume
  scene-empty frames during asynchronous startup; sampling begins only after
  renderable sections and texture dependencies are present.
- The policy reuses Stage 4's section world bounds, so it does not add asset
  ownership or backend-specific state. A depth pre-pass was not added because
  it would require a new common depth/color-write contract and has no measured
  payoff yet.
- The prior valid Sponza profile remains the comparison baseline:
  Vulkan Debug, 1093x695, CPU p50/p95 31.232/34.766 ms, G-buffer GPU p95
  4.698 ms, 725 draws and 725 sections. A fresh post-startup profile and
  inspected before/after image comparison are still required; an early CLI
  launch profile completed before asynchronous Sponza promotion and was
  discarded as invalid evidence.

## Stage 5 validation

- `cmake --build build --config Debug --target RenderPassScheduleTest` — passed.
- `RenderPassScheduleTest.exe` — 99/99 tests passed, including deterministic
  front-to-back ordering.
- `cmake --build build --config Debug --target KimPeanutEngine` — passed.
- Fresh Sponza runtime A/B profiling is pending because the validation launch
  remained in asynchronous startup/resource promotion without opening the
  local command transport; no timing or visual claim is made from that run.

## Stage 6.0 telemetry implementation checkpoint

- Added API-neutral graphics profile counters for descriptor search,
  allocation, and update work, plus command-recorder requested/emitted
  pipeline, mesh, resource-binding, and native-draw counts. Vulkan measures
  its descriptor-arena search and native allocation/update calls; OpenGL
  measures descriptor-set construction as its allocation/update equivalent.
- Added Render/FrameContext/MaterialSystem timing for section-candidate and
  visible-section preparation, directional shadow scheduling, material
  resolution, and mapped uniform writes. The fixed profile window now retains
  CPU-subphase p50/p95 values and the completion log emits the subphase and
  bind/descriptor counters.
- `cmake --build build --config Debug --target RenderSystemTest` — passed.
- `cmake --build build --config Debug --target RenderPassScheduleTest` — passed.
- `RenderPassScheduleTest.exe --gtest_color=no` — 100/100 tests passed,
  including CPU-subphase percentile coverage.
- `RenderSystemTest.exe --gtest_color=no` — 13/16 passed; three existing
  environment-fixture tests fail before scene promotion because their extra
  RGBA16F fixture makes `BuildPreparedCatalog()` return null. The diagnostic is
  `RenderSystem scene promotion requires prepared assets`; no Stage 6.0 code
  runs in those failures.
- Runtime fixed-window captures on Vulkan Debug/performance and OpenGL remain
  pending. No Stage 6.1+ optimization or performance claim is made from this
  instrumentation-only checkpoint.
