# Mesh Proxy Reconstruction TODO

**Status: planned; documentation only as of 2026-08-24.**

Design context: [component_module.md](component_module.md). This is a render
input reconstruction phase, intentionally before a general render graph.
Material System V1 ([material_system.md](../render/material_system.md)) is the
next prerequisite: it replaces `MeshProxy`'s temporary material alias before
the proxy registry stores material references.

## Phase MP1 — render-owned mesh proxy foundation

**Goal:** give `RenderSystem` a thread-safe registry of draw-ready mesh
renderables without reviving the deprecated OpenGL scene-proxy implementation.

- [x] Define generational `RenderableHandle`, concrete `MeshProxy`,
  `RenderableFlags`, and a render-private mesh/material reference shape.
  `AABB` (`std::array<float, 6>`) and `MaterialInstanceHandle` (`uint64_t`) are
  deliberate temporary aliases until their owning World/material systems exist
  (2026-08-24).
- [ ] Define value-based create/update/destroy command payloads. Transform,
  bounds, visibility, mesh/material identity, and shadow/opaque flags must be
  explicit; the payloads must not carry component pointers or native API types.
- [ ] Add a `RenderWorld`/renderable registry owned by `RenderSystem`. It is the
  sole owner of `MeshProxy` storage and validates all handles.
- [ ] Apply queued proxy commands only at the render-frame boundary, then expose
  an immutable frame snapshot for culling and draw-list construction.
- [ ] Add headless contract tests for stale/forged handles, update-after-destroy
  rejection, command ordering, and snapshot isolation.
- [ ] Add a temporary render-side producer so `GraphicsSmoke` can render one
  registered proxy before any world/component module is rebuilt.

**Done when:** the render module can register, update, remove, and snapshot a
mesh renderable without including world/component headers, raw OpenGL/Vulkan
types, or a direct cross-thread pointer.

## Phase MP2 — scene-pass draw list

**Goal:** replace bootstrap-scene-only submission with renderable-derived draw
work.

- [ ] Build a frustum-visible list from the immutable proxy snapshot.
- [ ] Classify opaque, transparent, and shadow-casting renderables.
- [ ] Sort opaque work by pipeline/material/mesh where the resulting order is
  legal for the pass.
- [ ] Make the current scene pass consume this list through `FrameContext` and
  `CommandRecorder` only.
- [ ] Keep static GPU resource ownership in render caches; proxies borrow
  resolved handles and never destroy them.

**Done when:** the current scene image is produced from registered mesh proxies,
not a hard-wired `RenderScene` demo instance.

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
