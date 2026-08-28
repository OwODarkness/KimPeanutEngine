# Material System V1 TODO

**Status: M1–M6 complete.** Design context:
[material_system.md](material_system.md). This ledger is intentionally scoped to
data-driven Material V1; it does not implement a material graph or deferred
shading yet. Progress checkpoint:
[gameplay-bootstrap-material-asset journal](../../.spec/journal/gameplay-bootstrap-material-asset.md).

## Phase M1 — handles and immutable templates

**Goal:** replace `MeshProxy`'s temporary material identity with a real,
render-owned, generational handle system.

- [x] Define `MaterialTemplateHandle` and `MaterialInstanceHandle` using the
  common generational handle system; remove the temporary `uint64_t`
  `MaterialInstanceHandle` alias from `MeshProxy` only when the new handle is
  wired end-to-end.
- [x] Define `MaterialDomain` (surface only in V1), blend mode (opaque and
  alpha blend), culling/double-sided state, and a data-driven shading-model
  field. Do not use a polymorphic `MaterialTemplate` base class.
- [x] Define immutable `MaterialTemplateDesc`: shader-program identity, fixed
  pipeline state, declared parameter layout, defaults, and pass compatibility.
- [x] Add `MaterialSystem`, owned by `RenderSystem`, with create/find/destroy
  operations for templates. Reject template destruction while an instance still
  references it.
- [x] Add headless tests for forged/stale template handles, duplicate/destroy
  behavior, immutable-description ownership, and template lifetime rejection.

**Done when:** a template has one render-owned identity and describes surface
policy without storing native API objects or World/component pointers.

## Phase M2 — data-driven instances and parameter validation

**Goal:** let many surfaces share one template while overriding typed values.

- [x] Define compact `MaterialParameterID` values resolved from template
  parameter names at template creation; draw recording must not perform string
  lookups.
- [x] Define V1 parameter kinds: scalar, vector/color, and texture+sampler
  reference. Use explicit tagged values rather than a PBR C++ subclass.
- [x] Define `MaterialInstanceDesc` as a template handle plus sparse overrides;
  missing values use template defaults.
- [x] Validate override ID, type, and logical texture/sampler references against
  the template. Reject unknown or mismatched parameters. GPU readiness is M3,
  when RenderSystem resolves the references through its caches.
- [x] Add create/update/destroy operations for instances. Instance destruction
  retires its values only; it never destroys the shared template, pipeline, or
  cached static texture/sampler resources.
- [x] Add headless tests for defaults, sparse overrides, type mismatch,
  template-instance lifetime, and stale instance handles.

**Done when:** two instances can share one immutable template and differ only
in validated parameter values.

## Phase M3 — pipeline and static resource resolution

**Goal:** connect material intent to existing RenderSystem caches without
letting Graphics choose material policy.

**Starting seam:** `RenderResourceResolver` now owns the current default
pipeline, mesh, texture, and sampler caches. M3 extends that render-private
resolver for material template/instance requests; `MaterialSystem` must not
receive a `RenderBackend` or cache container.

- [x] Resolve a template's shader program and fixed state to a render-side
  pipeline-cache key; request/bake `PipelineDesc` through the existing render
  pipeline path.
- [x] Resolve texture/sampler asset references through RenderSystem's static
  resource caches and keep the resulting common handles render-private.
- [x] Define readiness states for templates and instances: pending resources,
  ready, and failed with retained diagnostic text.
- [x] Classify instances as opaque or alpha blend for the future draw-list
  stage; do not add deferred-specific G-buffer behavior yet.
- [x] Test resolver request deduplication and a failed dependency that
  prevents the instance from being submitted.

**Done when:** a ready instance supplies a common pipeline handle plus static
texture/sampler handles, while Graphics sees only the final descriptions and
handles.

## Phase M4 — frame-local material bindings

**Goal:** turn a ready material instance into bindings valid for one frame slot.

- [x] Define a method that packs validated instance constants into a
  `FrameContext` uniform allocation and builds a `ResourceBindingSetDesc`.
- [x] Let `FrameContext` create and own the resulting
  `DescriptorSetHandle`/binding-set handle. A material instance must not cache
  it across frames.
- [x] Record material binding through `CommandRecorder` after binding the
  compatible pipeline and before drawing a mesh.
- [x] Verify normal frame-slot reuse, resize, and teardown on Vulkan and
  OpenGL with `GraphicsSmoke` (2026-08-26).
- [x] Add a contract guard that rejects reuse of a frame-local material binding
  after its frame slot retires.

**Done when:** persistent material data produces transient bindings with the
same lifetime rules as the existing `FrameContext` resources. M4 landed and
`GraphicsSmoke` passes on Vulkan and OpenGL.

## Phase M5 — MeshProxy and draw-list integration

**Goal:** make mesh renderables reference real material instances.

- [x] `MeshProxy` already stores the real `MaterialInstanceHandle` from M1.
- [x] Extend future create/update renderable commands with material-instance
  identity; reject a draw when mesh or material is not ready.
- [x] Build opaque draw sorting around resolved pipeline → material instance →
  mesh. `SceneDrawListBuilder` keeps alpha-blend candidates separate and
  preserves their snapshot order until a camera-depth sort is introduced.
- [x] Retire the bootstrap scene's ad-hoc pipeline/texture material binding
  once a registered mesh proxy can render through Material V1.

**Done when:** a registered mesh proxy is drawn through a ready material
instance, without any World-to-Graphics dependency or backend-native material
object. Opaque pipeline/material/mesh sorting landed 2026-08-26; transparent
depth sorting and future World commands continue with Mesh Proxy MP2/MP3.

## Phase M6 — Material Asset V1

**Goal:** replace Gameplay's transitional `MaterialInstanceHandle` input with
a stable, serialized material asset selected by `AssetID`.

### Asset format and loading

- [x] Add one `*.material` metadata format and an Asset loader/resource type.
  The file owns only authoring data: shader-program asset reference, surface
  domain/shading model, fixed blend/cull/double-sided policy, and typed default
  parameters whose textures are asset references.
- [x] Define a small versioned schema and diagnostics for malformed files,
  unknown fields, incompatible parameter types, missing shader/texture
  references, and unsupported values. Keep the first format declarative; do
  not embed C++ class names, graph nodes, pipeline handles, descriptor sets, or
  backend-specific data.
- [x] Reuse the existing Asset → Resource → Render flow. Asset owns parsed CPU
  material data and identity; Render owns template/instance creation and
  resource readiness; Graphics receives only final common RHI descriptions.

### Gameplay and render integration

- [x] Replace `render::MaterialReference` in gameplay source descriptions and
  `StaticMeshActorDesc` with a material `asset::AssetID`. Factories and
  components must never receive `MaterialInstanceHandle`.
- [x] Make `RenderSystem` resolve each material asset to one private immutable
  template plus the default instance, deduplicated by material asset identity.
  A future per-Actor override becomes a separate authored override value, not
  a public render-instance handle.
- [x] Preserve source state semantics: unresolved material asset is Pending;
  parse/validation/resource failure is Failed with a retained diagnostic; no
  proxy is created in either state.
- [x] Migrate the bootstrap actor to reference a bootstrap `*.material` file
  rather than calling `CreateDefaultTexturedMaterial`. Remove that bootstrap
  special-case helper once the asset path is live.

### Validation and scope limits

- [x] Add AssetUnitTest coverage for a valid unlit textured material plus
  unknown-field and invalid-parameter rejection.
- [x] Add malformed-version/reference diagnostics and MaterialSystem-adjacent
  tests for material-asset deduplication, unloaded identity, and
  pending/failed diagnostics (M6.1, 2026-08-28).
- [x] Extend `GameplayUnitTest` so `CreateStaticMeshActor` accepts only asset
  identities. `GraphicsSmoke` verifies the AssetID-only gameplay-to-render
  source boundary on Vulkan and OpenGL; an interactive bootstrap run remains
  separate validation.
- [x] Update module/status documentation with the schema, ownership/lifetime,
  and migration evidence.

**M6 core integration landed 2026-08-28:** Gameplay components/factories now
publish a material AssetID. RenderSystem loads relative shader/texture paths,
builds one cached template plus default instance per material asset, and uses
the instance only in its private MeshProxy. The bootstrap scene preloads and
references `material/bootstrap.material`. V1 deliberately accepts one texture
parameter named `base_color_texture`, mapped to the existing unlit binding 2.

**M6.1 validation landed 2026-08-28:** `MaterialAssetResolver` now owns the
render-private AssetID → template/default-instance cache as a separately
testable unit. Coverage proves one record per material asset, invalid/pending/
broken-reference diagnostics, unsupported material-version rejection, and
ready-proxy retirement when later resolution fails.

**Reference check:** O3DE's `MaterialSourceData` is versioned authoring data
built into a runtime material asset. KimPeanut adopts that ownership split but
deliberately omits O3DE's offline asset-builder layer: this tiny engine parses
the versioned JSON into a CPU `MaterialResource`, then Render creates the
runtime template/default instance on demand.

**Done when:** a game author can create a static-mesh Actor using mesh and
material asset IDs only; Render privately derives all `MaterialTemplateHandle`,
`MaterialInstanceHandle`, pipeline, texture/sampler, and frame-local binding
state.

## Explicitly deferred

- Material graph/node editor and shader generation.
- Bindless resources, virtual textures, parameter arrays, and push-constant
  policy.
- PBR G-buffer encoding, lighting, shadows, and post-process.
- Material domains beyond surface (decal, UI, terrain, water, hair).

Those features become separate phases only after Material V1 has a real
MeshProxy consumer.
