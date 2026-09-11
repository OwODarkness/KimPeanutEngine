# Live2D Module Plans

**Status: active implementation.** This document records the analysis and architecture for
an optional Live2D module. Current work is tracked in [TODO.md](TODO.md), the
SDK design is in [`.plan/L2D1.md`](.plan/L2D1.md), the generic Asset migration
is owned by [AX1](../asset/.plan/AX1.md), Live2D's Asset handoff is staged in
[`.plan/L2D2.md`](.plan/L2D2.md), Live2D planning and generic submission are staged in
[`.plan/L2D4.md`](.plan/L2D4.md), and the complete V1 execution contract is in
[`.spec/specs/live2d-v1-rendering.md`](../../.spec/specs/live2d-v1-rendering.md).

## Outcome

KimPeanutEngine should gain a standalone `Live2D` module, composed like TTS but
with narrower internal targets for product data, import, runtime model state,
and rendering. A `.model3.json` package is imported offline into a versioned
native Live2D asset. Runtime loads that product through the ordinary
`AssetManager`, creates independent mutable character instances, and renders
them through the engine RHI. V1 ends with one model rendered correctly in a
dedicated Live2D viewer window on OpenGL and Vulkan.

V1 is intentionally rendering-only. Motion playback, expression policy,
emotion mapping, body-language selection, eye tracking, lip-sync, TTS coupling,
physics authoring, hit testing, and gameplay components are future stages.

## Current-state analysis

### Asset extension prerequisite is landed

AX1 replaced the former closed payload variant with `shared_ptr<IAssetPayload>`
and added Asset-owned runtime/importer registries. L2D2 consumes that boundary
with module-owned type value `0x1000`, `.live2d` loader, and `.model3.json`
provider; L2D3 connects it to the offline tool. Asset contains no Live2D branch.
The renderer therefore consumes an already loaded `Live2DModelResource` and
ordered Texture dependencies; it must not reopen source files or redesign Asset.

### Graphics has most static draw concepts but not Live2D streaming geometry

The common RHI already has pipeline descriptions, texture/sampler bindings,
offscreen render targets, viewport/scissor, indexed draws, and fixed blend
state. Live2D additionally needs:

- frame-safe updates of deformed vertices without recreating meshes;
- a generic 2D vertex layout instead of the fixed PBR `data::Vertex` payload;
- stable index buffers and per-drawable indexed ranges;
- normal, additive, and multiplicative blend policies;
- mask-atlas rendering followed by sampled clipping;
- a viewer presentation path that does not expose a native API through the
  Live2D public interface.

Those are general Graphics capabilities with a Live2D consumer. They belong in
the common RHI and both backend implementations, not in an OpenGL-only shortcut
inside Asset or common Live2D code.

### A Live2D package is a compound source, not one static image

`.model3.json` is the source root. It references a `.moc3`, ordered texture
atlases, and may reference expressions, motions, physics, pose, user data,
display information, and motion-sync data. The `.moc3` contains the parameter-
driven model and drawable geometry; Core evaluates it into current vertices.
The source root therefore needs dependency-aware import even when V1 only
executes the render subset.

## SDK selection

Use the official **Live2D Cubism 5 SDK for Native R5**, release tag `5-r.5`, as
the V1 baseline. As of 2026-09-08 it is the current stable Native release; the
advertised Cubism 5.4 line is alpha and is not the default integration target.

The SDK has three relevant pieces:

- **Cubism Core**: proprietary native library and headers. It loads `.moc3`,
  evaluates parameters, and exposes drawable vertices and metadata; it does
  not draw.
- **Cubism Native Framework**: source-available C++ utilities for settings,
  models, motions, effects, physics, and reference renderers.
- **Cubism Native Samples**: executable examples for OpenGL, Vulkan, and other
  APIs.

CMake should expose `KPENGINE_ENABLE_LIVE2D=ON` by default for engine
development, with an explicit `OFF` escape hatch for CI or machines without a
local SDK. `KPENGINE_CUBISM_SDK_ROOT` remains a local configuration input.
Configuration validates the matching Core header/library/runtime binary and
pinned Framework release. Proprietary Core is not fetched automatically or
committed. Required runtime DLL copying uses the existing runtime-dependency
mechanism and only for an enabled Live2D target.

Production code uses Core plus the backend-neutral Framework model facilities.
It does not ship the official OpenGL or Vulkan renderer as the engine renderer:
those renderers own native API state and cannot preserve KimPeanutEngine's
runtime-selectable, API-neutral RHI boundary. A tiny official-renderer probe may
exist temporarily to validate SDK installation, but it is not an Asset or V1
shipping path.

## Proposed module and target layout

```text
engine/module/live2d/
  product/       versioned native product schema and defensive parser
  import/        .model3.json source closure and offline conversion
  runtime/       Cubism lifecycle, immutable asset payload, mutable instances
  render/        RHI draw extraction, mask/main passes, GPU lifetime
  editor/        Live2D-specific editor panels and preview composition
  register/      explicit Asset/import-provider registration
  CMakeLists.txt

engine/example/live2d/
  dedicated viewer composition and command-line entry point
```

Proposed targets and dependencies:

```text
Live2DProduct  -> Core/Data only; no Cubism, AssetManager, Render, or Graphics
Live2DImport   -> Live2DProduct + AssetImport/ImageIO/JSON + Cubism validation
Live2DRuntime  -> Live2DProduct + AssetRuntime + Cubism Core/Framework
Live2DRender   -> Live2DRuntime + Render/Graphics common contracts
Live2D         -> INTERFACE aggregate, analogous to Module/TTS composition
Live2DModule   -> EngineModule lifecycle; owns Live2DSystem and Cubism lifetime
Live2DEditor   -> Live2D + generic Editor extension registry; module bootstrap invokes registration
Live2DViewer   -> Live2D + Window + viewer-only presentation adapter
```

The application is the composition root for statically linked modules. Its
bootstrap function creates each enabled `EngineModule` and transfers ownership
to `Engine`; `Engine` schedules initialization, game-thread ticks, and
reverse-order shutdown. A module owns its runtime system and may separately
register editor extensions through the generic editor registry. This is the
migration seam for a future dynamic module loader: the engine lifecycle does
not need to know how a module was discovered or constructed.

The importer remains usable without the running engine. The runtime target
does not depend on the authoring archive/database. The render target never
opens model files or constructs Asset identity.

## Asset integration boundary

The generic Asset extensibility design belongs to Asset and is specified in
[AX1](../asset/.plan/AX1.md). Live2D does not define `AssetType`, payload
erasure, registry sealing, suffix collision policy, loader ownership, cache
transactions, or importer-provider infrastructure.

Live2D's responsibility is limited to supplying Live2D-specific registrations:

```text
RegisterLive2DAssetTypes(AssetTypeRegistry&)    // .live2d runtime product
RegisterLive2DImporters(AssetImporterRegistry&) // .model3.json source import
```

The Live2D registration must provide a stable type value, the canonical
`.live2d` native suffix, `Live2DModelResource` implementing `IAssetPayload`, a
native-product loader, and an offline importer provider. Asset remains the
owner of identity, `AssetID` creation, dependency requests, rollback,
observation, cache lifetime, and unload behavior.

The L2D2 implementation provides a versioned product codec, an immutable
payload, explicit registrations, and a database-free source provider. L2D3
connects that provider to the offline `KimPeanutAssetTool import-live2d`
command. The command stages the provider output, validates immutable
content-addressed collisions, publishes native Texture products, and writes
the `.live2d` root last. Runtime and RHI rendering remain later stages. The
Live2D-specific integration sequence and acceptance criteria are in
[L2D2](.plan/L2D2.md). The generic migration gates, lock policy, and payload
tests are in [AX1](../asset/.plan/AX1.md).

## Native Live2D product

V1 imports `.model3.json` into a deterministic, versioned `.live2d` root
product. It contains:

- magic, schema version, required feature flags, and bounded chunk table;
- original Cubism model version/compatibility metadata;
- exact `.moc3` bytes consumed by Cubism Core;
- ordered native Texture asset references matching Cubism texture indices;
- canonical layout and canvas metadata needed before instance creation;
- optional named byte chunks for expressions, motions, physics, pose, user
  data, display information, and motion-sync data;
- a source-closure/import-settings digest and payload integrity digest.

V1 executes only `.moc3`, texture, layout, and render metadata, but preserving
typed optional chunks prevents the importer from discarding authored behavior
that later stages need. Unknown optional roles may be preserved; unknown
required feature flags are rejected.

The provider and offline command:

1. normalizes the `.model3.json` path under the Asset root;
2. parses and validates every relative reference without allowing path escape;
3. fingerprints the complete referenced source closure and importer/schema
   versions;
4. validates `.moc3` compatibility against the selected Core release;
5. imports/cooks texture atlases through the existing native Texture path;
6. writes and re-reads a staged deterministic `.live2d` product;
7. validates existing immutable products before publication;
8. publishes native Texture products and the root product last.

The command requires an explicit output path, for example:

```text
KimPeanutAssetTool import-live2d \
  --source live2d/hiyori_pro/runtime/hiyori_pro_t11.model3.json \
  --output content/hiyori_pro.live2d \
  --asset-root asset
```

The output's sibling `.archive` directory is the publication root. An
explicit `--archive-root` is accepted only when it names that sibling
directory, keeping the product's dependency paths valid without reaching into
`ModelArchiveDatabase` internals.

## Runtime ownership and data flow

```text
.live2d path
  -> Asset type registry
  -> Live2DAssetLoader
  -> immutable Live2DModelAsset + ordered Texture dependency requests
  -> AssetManager registration/cache/reference graph
  -> Live2DSystem::CreateInstance(asset id)
  -> mutable Live2DModelInstance (Cubism model and parameters)
  -> Live2DRenderPlanner calculates semantic draw/mask plan
  -> Live2DRenderSubmission wraps generic ordered RenderSubmission work
  -> generic RenderSubmissionExecutor + FrameContext
  -> Graphics CommandRecorder
  -> OpenGL or Vulkan backend
```

Ownership is intentionally split by semantic, resource, and frame lifetime:

| Object | Owner | Lifetime and mutation |
| --- | --- | --- |
| `Live2DModelAsset` | Asset cache/shared payload | Immutable; may outlive its Asset wrapper through `shared_ptr`. |
| `Live2DModelInstance` | `Live2DSystem` caller/instance pool | Mutable per character; owns Cubism parameter/model evaluation state. |
| `Live2DRenderResourceSet` | Live2D render service/composition | Shared pipeline variants and sampler; outlives every proxy/submission. |
| `Live2DRenderProxy` | Live2D render caller | Model-specific GPU handles/static ranges; retired only after submitted work is safe. |
| `Live2DRenderSubmission` | Frame producer until execution returns | Owns Live2D diagnostics and nested generic work; no SDK pointers. |
| `RenderSubmissionExecutor` | Generic Render frame path | Borrows submission/resources, allocates `FrameContext` data, records common commands. |

Multiple instances may share one asset and texture dependencies but never share
mutable Cubism model parameters. Asset dependency edges protect Texture assets
while the root wrapper is live; instance-held payload `shared_ptr`s keep the
immutable model and Texture CPU data valid if that wrapper is later unloaded.

Cubism Framework uses a module-owned allocator and log bridge. Framework
startup/initialize happens once in `Live2DSystem::Initialize`; shutdown first
stops instance updates, destroys render proxies, destroys instances, disposes
Framework, and finally releases Core/module state.

## V1 render architecture

The Live2D planner reads the current Core/Framework drawable data rather than
calling an official native renderer. It owns the features required to describe
a correct still frame:

- drawable visibility and render-order sorting;
- current positions, UVs, indices, texture index, opacity, and culling;
- model, multiply, and screen colors;
- normal, additive, and multiplicative blend modes;
- inverted and ordinary clipping masks;
- model/canvas transform, viewport, resize, and transparent background;
- explicit rejection of an unsupported required Cubism 5.3/R5 offscreen
  drawing feature rather than silent corruption.

The common Graphics and Render additions are specified by
[L2D4](.plan/L2D4.md): generic
immutable/per-frame buffers plus an explicit multi-stream geometry view, not
public `MapVkBuffer`/`glBufferSubData` hooks. Static UV/index data is uploaded
once. One common per-frame position handle owns backend frame-slot storage and
is written only after `BeginFrame` selects a safe slot. Indexed ranges and
base-vertex offsets let one concatenated model buffer serve all drawables while
the existing static `MeshHandle` path remains compatible.

Above that primitive, Live2D compiles its semantic plan into an already ordered,
value-only `render::RenderSubmission`. Generic Render validates it, allocates
frame-local uniform/binding data, and records it through `CommandRecorder`.
Render sees only uploads, targets, geometry, pipeline handles, resources,
viewport/scissor, and indexed draws. It has no Cubism drawable, parameter,
expression, mask-context, or feature-policy concept. Per-pixel texture/color/
mask work remains in the GPU shaders.

V1 uses correctness-oriented pipeline variants for mask, normal, additive, and
multiplicative draws. Pipeline-switch elimination, bindless batching, CPU-
visible VRAM specialization, and render-graph scheduling require profile
evidence and are not V1 goals.

Clipping uses one module-owned RGBA mask atlas per render proxy or compatible
shared allocation. A mask pass clears and populates packed regions/channels;
the model pass samples it. Mask allocation, target format, alpha convention,
and blend equations must be verified against the official R5 sample output.

## Dedicated viewer boundary

`KimPeanutLive2DViewer` is a separate executable, not a mode selected by
uncommenting `main.cpp`. It accepts at least:

```text
--asset <path-to-native.live2d>
--graphics-api opengl|vulkan
--width <pixels> --height <pixels>
```

The viewer owns one window, one Graphics backend, one Live2D system, one model
instance, and one renderer. A viewer-only presentation adapter may use the
existing Editor/ImGui presentation bridge to display the module's offscreen
color target; this dependency must not leak into `Live2DProduct`,
`Live2DRuntime`, or `Live2DRender` public contracts.

The viewer is both the V1 user outcome and the runtime validation harness. It
must support screenshot capture through the engine capture boundary so visual
evidence can compare OpenGL, Vulkan, and an official reference render.

## Reference findings

### Official Cubism SDK

- The [Cubism SDK manual](https://docs.live2d.com/en/cubism-sdk-manual/top/)
  records Cubism 5 SDK for Native R5 as the stable Native release on 2026-04-02
  and separately advertises 5.4 alpha. Pinning R5 avoids making an alpha the
  engine baseline.
- The [Core API reference](https://docs.live2d.com/en/cubism-sdk-manual/cubism-core-api-reference/)
  states that Core loads `.moc3`, calculates vertices, does not provide drawing,
  and relies on caller-provided allocation. That supports a custom RHI renderer
  and an explicit module allocator/lifecycle.
- The [Native samples](https://github.com/Live2D/CubismNativeSamples/tree/b8024738f108e6003e4925193e8d5ec04cd18196)
  provide OpenGL and Vulkan implementations and require a separately downloaded
  Core package. They are the conformance baseline, not a cross-API engine seam.
- The [Framework R5 source](https://github.com/Live2D/CubismNativeFramework/tree/145155d2c5bdd8d23475cef9cc3ab46d3220190c)
  separates model/motion/physics facilities from platform renderers. Its
  release notes also show renderer lifecycle and Vulkan device-state changes,
  evidence that wrapping its native renderer would couple the module to SDK
  backend internals.
- The official [embedded-data documentation](https://docs.live2d.com/en/cubism-editor-manual/export-moc3-motion3-files/)
  and [file-type reference](https://docs.live2d.com/en/cubism-editor-manual/file-type-and-extension/)
  establish `.model3.json` as the reference root for `.moc3`, textures, and
  optional behavior files. This supports a compound importer rather than a
  direct `.moc3` loader.
- The [Framework license](https://github.com/Live2D/CubismNativeFramework/blob/145155d2c5bdd8d23475cef9cc3ab46d3220190c/LICENSE.md)
  distinguishes the Open Software license for components from the proprietary
  Core license and identifies a separate release-license condition for some
  business users. Licensing is therefore a build/release gate, not a README
  footnote.

### Sakura Engine

The Sakura study used commit
[`c0fdb2bb`](https://github.com/SakuraEngine/SakuraEngine/tree/c0fdb2bb30074058cb98df98b8134559d13d67b0):

- [`build.cs`](https://github.com/SakuraEngine/SakuraEngine/blob/c0fdb2bb30074058cb98df98b8134559d13d67b0/engine/modules/render/live2d/build.cs)
  defines a standalone `SkrLive2D` module, keeps Cubism Framework private, and
  depends publicly on image and renderer services. The standalone-module shape
  applies directly.
- [`l2d_model_resource.h`](https://github.com/SakuraEngine/SakuraEngine/blob/c0fdb2bb30074058cb98df98b8134559d13d67b0/engine/modules/render/live2d/include/SkrLive2D/l2d_model_resource.h)
  separates CPU model data and asynchronous RAM completion from its GPU render
  model.
- [`l2d_render_model.h`](https://github.com/SakuraEngine/SakuraEngine/blob/c0fdb2bb30074058cb98df98b8134559d13d67b0/engine/modules/render/live2d/include/SkrLive2D/l2d_render_model.h)
  owns textures, vertex/index views, clipping state, draw commands, and binding
  caches in a render model. This supports KimPeanutEngine's Asset/instance/
  render-proxy split.
- [`live2d_render_effects.cpp`](https://github.com/SakuraEngine/SakuraEngine/blob/c0fdb2bb30074058cb98df98b8134559d13d67b0/engine/modules/render/live2d/src/live2d_render_effects.cpp)
  reads current drawable positions/UVs, updates dynamic vertex storage, builds
  mask and model passes, and submits through Sakura's renderer rather than the
  official native renderer. The custom-renderer principle applies.
- The separate
  [`live2d-viewer`](https://github.com/SakuraEngine/SakuraEngine/blob/c0fdb2bb30074058cb98df98b8134559d13d67b0/engine/samples/application/live2d-viewer/src/live2d_viewer.cpp)
  composes window/UI, I/O, model, and renderer services. That is a useful V1
  validation boundary.

Sakura's render graph, ECS storage, DirectStorage decompression, asynchronous
RAM/VRAM services, CPU-visible VRAM optimization, and pipeline-switch target do
not transfer to V1. KimPeanutEngine currently has a fixed pass schedule and a
smaller RHI. Copying those systems would expand scope without evidence; V1
adopts the ownership/data-flow pattern and implements only the required common
Graphics capabilities.

## Rejected designs

- **Add `KPAT_Live2D` and `Live2DPtr` directly to Asset:** makes a core runtime
  module know an optional feature and leaves future asset types closed.
- **Load `.model3.json` directly in `AssetManager`:** turns runtime load into a
  source-package importer, repeats image decode, and makes deployment depend on
  authoring layout.
- **Store a mutable Cubism model in the shared asset:** two character instances
  would overwrite the same parameters, motion state, and vertices.
- **Use the official OpenGL renderer as V1 architecture:** prevents one build
  from selecting OpenGL or Vulkan at runtime and bypasses RHI GPU ownership.
- **Compile both official OpenGL and Vulkan Framework renderers into one static
  target:** they define the same renderer factory symbols and still expose
  incompatible native lifecycle assumptions.
- **Put Cubism/Vulkan/OpenGL types in the common Live2D API:** leaks SDK/backend
  details upward and makes tests/tooling depend on platform headers.
- **Create/destroy meshes every frame:** violates efficient and safe GPU
  lifetime expectations; deformed vertices need frame-safe streaming storage.
- **Let a Live2D renderer record `CommandRecorder` directly:** couples semantic
  planning to frame allocation/execution and leaves no reusable submission seam.
  Live2D instead emits ordered generic work that Render executes without
  knowing its source.
- **Add Live2D cases to `RenderSystem` or the fixed deferred pass enum:** makes
  generic Render know an optional module and expands the scene renderer for a
  standalone consumer. Main-engine scheduling remains later L2D10 work.
- **Implement emotion, body language, or TTS coupling during V1:** those are
  behavior-policy systems above a verified model-instance and rendering base.
- **Import Sakura's render graph or I/O stack:** those designs solve Sakura's
  scale and architecture, not the current KimPeanutEngine prerequisite.

## Long-term stages after V1

After the viewer/render baseline is correct, later work can add:

1. motion and expression assets plus deterministic playback APIs;
2. physics, pose, eye blink, gaze, hit areas, and user-data support;
3. emotion/body-language policy that selects and blends authored behaviors;
4. audio amplitude/phoneme-driven lip-sync and optional TTS integration;
5. Gameplay components, Editor inspectors, hot reimport, packaging, and scene
   composition;
6. measured batching, async streaming, and mask/render performance work.

These features consume the V1 instance API; none belong in Asset core.
