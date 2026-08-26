# Mesh Proxy Reconstruction TODO

**Status: MP1 complete; MP2 basic submission and CPU frustum visibility complete as of 2026-08-26.**

Design context: [component_module.md](component_module.md). This is a render
input reconstruction phase, intentionally before a general render graph.
Material System V1 M1–M2 ([material_system.md](../render/material_system.md))
is complete: `MeshProxy` already carries its real render-owned material handle,
and instances now validate sparse typed overrides. The next material
prerequisite for drawing is M3–M4 resource/pipeline and frame-binding
resolution.

## Phase MP1 — render-owned mesh proxy foundation

**Goal:** give `RenderSystem` a thread-safe registry of draw-ready mesh
renderables without reviving the deprecated OpenGL scene-proxy implementation.

- [x] Define generational `RenderableHandle`, concrete `MeshProxy`,
  `RenderableFlags`, and a render-private mesh/material reference shape.
  `world_bounds` uses the canonical `spatial::AABB` from `core/spatial`, so
  Physics, World, Render, and editor tooling can share one bounds contract;
  `MaterialInstanceHandle` is a real render-owned generational handle
  (2026-08-26).
- [x] Define value-based create/update/destroy command payloads. Transform,
  bounds, visibility, mesh/material identity, and shadow/opaque flags must be
  explicit; the payloads must not carry component pointers or native API types.
- [x] Add a `RenderWorld`/renderable registry owned by `RenderSystem`. It is the
  sole owner of `MeshProxy` storage and validates all handles.
- [x] Apply queued proxy commands only at the render-frame boundary, then expose
  an immutable frame snapshot for culling and draw-list construction.
- [x] Add headless contract tests for stale/forged handles, update-after-destroy
  rejection, command ordering, and snapshot isolation.
- [x] Add a temporary render-side producer so `GraphicsSmoke` can render one
  registered proxy before any world/component module is rebuilt.

**Done when:** the render module can register, update, remove, and snapshot a
mesh renderable without including world/component headers, raw OpenGL/Vulkan
types, or a direct cross-thread pointer.

## Phase MP2 — scene-pass draw list

**Goal:** replace bootstrap-scene-only submission with renderable-derived draw
work.

- [x] Build the initial no-culling list from the immutable proxy snapshot: every
  visible proxy with a valid mesh and ready material is submitted. Frustum
  culling is deliberately deferred.
- [x] Add conservative CPU frustum culling before draw submission.
  `SceneVisibility` derives a `Frustum` with six normalized planes from the
  pass camera's non-transposed view-projection matrix and rejects only AABBs
  definitely outside a plane; malformed bounds remain visible rather than
  disappearing. It is render policy only: no backend-native type, occlusion
  query, world-partition, or LOD state is involved (2026-08-26).
- [x] Classify opaque and alpha-blend renderables through the material draw
  class. Alpha-blend candidates remain separately ordered by their immutable
  snapshot until a pass-specific depth sort is added.
- [x] Sort opaque work by resolved pipeline → material instance → mesh, where
  the resulting order is legal for the pass.
- [x] Make the current scene pass consume this list through `FrameContext` and
  `CommandRecorder` only.
- [x] Keep static GPU resource ownership in render caches; proxies borrow
  resolved handles and never destroy them.

**Done when:** the current scene image is produced from registered mesh proxies,
not a hard-wired `RenderScene` demo instance. CPU frustum culling landed
2026-08-26; opaque classification and sorting landed the same day. Shadow
classification and transparent depth sorting remain MP2 work.

## Phase MP3 — world component reconstruction

**Goal:** let a future world/component system produce renderable commands without
coupling it to graphics implementation details.

- [ ] Reintroduce the minimum scene/primitive component lifecycle required for
  registration: activate/create, dirty update, deactivate/destroy.
- [ ] Implement `MeshComponent` as a logical mesh/material/transform owner and
  queue producer; it does not own a proxy, GPU handle, or draw method.
- [ ] Define asset/material readiness behavior: an incomplete asset reference
  produces no proxy draw until RenderSystem resolves the required render cache
  entries.
- [ ] Define teardown ordering for component destruction, world unload, render
  command drain, and render-registry retirement.

**Done when:** the world can add a mesh to a scene by adding/configuring a
`MeshComponent`, while rendering sees only the resulting render-world snapshot.

## After MP3

Add shadow, G-buffer/deferred, lighting, and post-process passes over the draw
lists. Once multiple real passes consume proxy-derived resources, evolve the
current render pass schedule into a render graph.
