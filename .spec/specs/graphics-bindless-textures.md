# Graphics Bindless Textures

- Status: active
- Owner: Amadeus / Codex
- Parent TODO: [Bindless textures — planned, optional path](../../docs/graphics/TODO.md#bindless-textures--planned-optional-path)

## Objective

Add an optional, portable bindless sampled-texture path for material shaders.
Materials will refer to a Graphics-owned table through common generational
slots, while the existing per-draw `ResourceBindingSetDesc` path remains a
correct fallback on unsupported devices and for exhausted table capacity.

## Current state

- `GraphicsCapabilities` reports the effective common-RHI capability surface.
  `bindless_textures` is deliberately `false` in both Vulkan and OpenGL.
- The current material path resolves ordinary `TextureHandle` and
  `SamplerHandle` values into frame-local `ResourceBindingSetDesc` bindings.
- `FrameContext` already ties transient binding-set lifetime to backend frame
  slots. Graphics backends own GPU resource lifetime and native API state.
- `docs/graphics/TODO.md` contains the B0–B5 roadmap. No bindless API, shader
  ABI, descriptor-indexing feature enablement, or OpenGL resident-handle path
  exists yet.

## Scope and non-goals

In scope:

- A sampled-texture-only common table contract, lifetime rules, backend
  implementations, material adoption, fallback, and validation.
- A stable shader-table ABI and opt-in material/pipeline convention shared by
  Vulkan and OpenGL.

Out of scope for V1:

- Bindless buffers, storage images, acceleration structures, and generic
  descriptor arrays.
- Native API handles or descriptor objects in Render, Editor, materials, or
  the common shader-facing material data.
- GPU-driven rendering, indirect submission, virtual texturing, or a general
  descriptor-heap abstraction.

## Invariants

- Graphics owns the GPU-visible table, native descriptor/resident-handle
  state, and deferred slot reuse; Render owns material policy and fallback
  selection.
- A common `BindlessTextureHandle` is generational. A released slot cannot
  silently select a newer texture, and it cannot be reused before GPU work that
  could read its old entry is complete.
- `TextureHandle` and `SamplerHandle` remain the inputs to Graphics. Vulkan
  objects, descriptor-indexing structures, and `GLuint64` handles stay private
  to their backends.
- Bindless is opt-in. Unsupported capability, incompatible shader convention,
  stale slots, or capacity exhaustion must select the ordinary bound path or
  fail material resolution explicitly; no draw may sample an unintended
  texture.
- The shader-visible table layout, set/binding convention, and ABI version are
  explicit and shared across backends. Ordinary material bindings must continue
  working unchanged.
- Slot updates and retirement integrate with the existing frame lifecycle; no
  global `WaitIdle` is introduced as normal operation.

## Stages

1. **B0 — common contract.** Specify the handle representation, capacity,
   allocation/release result model, capability facts, table ABI/version, and
   material fallback semantics. Add common contract tests before enabling a
   backend.
2. **B1 — lifetime protocol.** Define visibility of writes, replacement and
   release, frame-slot retirement fences, bounded reuse, exhaustion telemetry,
   and deterministic test limits. **Complete 2026-08-27:** the common allocator
   quarantines released generational slots behind submission serials; native
   table writes and destruction remain backend-private B2/B3 work.
3. **B2 — Vulkan.** Probe and enable only the descriptor-indexing feature
   subset required by B0; implement backend-private descriptor table and
   frame-safe allocator; reserve and bind the global table in compatible
   pipeline layouts; connect retirement to the actual completed frame fence;
   make capability reporting reflect the enabled path only after all of those
   pieces have runtime evidence. This is one atomic milestone, not a sequence
   of independently releasable partial tables.
4. **B3 — OpenGL.** Select and validate its bindless-texture extension route,
   own resident handle/table state privately, and match the common lifetime and
   fallback semantics.
5. **B4 — Render/material adoption.** Add serializable opt-in metadata and
   cache slots through material resolution. `FrameContext` chooses bindless or
   ordinary bindings; `MeshProxy` remains unaware of native bindings.
6. **B5 — evidence and rollout.** Add unit/contract coverage plus a real
   multi-material smoke scene on Vulkan and OpenGL. Keep the feature opt-in
   until both paths and the fallback have runtime evidence.

## Acceptance criteria

- [x] The public common contract is sampled-texture-only, generational, and
  free of backend-native types.
- [x] The fallback is deterministic for unavailable, incompatible, stale, and
  exhausted bindless requests.
- [x] Vulkan and OpenGL report support only after their complete native paths
  honor the same common contract.
- [x] Released entries are not reused before all potentially reading submitted
  work completes.
- [ ] Compatible shaders produce equivalent output through bindless and bound
  paths in the representative multi-material scene.
- [ ] Unit/contract tests cover handles, capacity, fallback, and retirement;
  runtime smoke covers both backends when supported.

## Validation plan

- B0/B1 documentation and common contract design: L0 review; common-handle
  code raises this to L2 with `GraphicsContractTest`.
- Backend/table or shader ABI changes: L3 — `GraphicsContractTest` plus
  `GraphicsSmoke` on Vulkan and OpenGL. Explicitly record unsupported-feature
  skips rather than treating them as passes.
- Any shared public RHI contract or multi-module lifecycle change: escalate to
  L4 per [the validation matrix](../../docs/validation_matrix.md).

## Risks and open questions

- The exact Vulkan descriptor-indexing feature subset and descriptor update
  synchronization policy must be chosen against the required shader ABI.
- OpenGL extension availability and driver behavior may make the path less
  portable than Vulkan; capability reporting must remain conservative.
- The project needs a precise definition for when a table update becomes
  visible to a recording/submitted frame, especially while a material changes
  texture or sampler.
- Table capacity, growth policy, and descriptor pool budgeting need measurable
  limits before the public API is fixed.
- Shader preprocessing currently needs a versioned, testable convention rather
  than backend-specific source injection.
