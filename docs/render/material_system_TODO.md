# Material System V1 TODO

**Status: M1–M4 complete as of 2026-08-26.** Design context:
[material_system.md](material_system.md). This ledger is intentionally scoped to
data-driven Material V1; it does not implement a material graph or deferred
shading yet.

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

## Explicitly deferred

- Material graph/node editor and shader generation.
- Bindless resources, virtual textures, parameter arrays, and push-constant
  policy.
- PBR G-buffer encoding, lighting, shadows, and post-process.
- Material domains beyond surface (decal, UI, terrain, water, hair).

Those features become separate phases only after Material V1 has a real
MeshProxy consumer.
