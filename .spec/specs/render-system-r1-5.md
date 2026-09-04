# RenderSystem R1.5 Facade Hardening

- Status: implementation complete; orderly close evidence pending (2026-09-04)
- Owner: project maintainers / implementing agent
- Parent TODO: [Render R1.5](../../docs/render/TODO.md)
- Stage design: [R1.5](../../docs/render/.plan/R1.5.md)
- Predecessor: [R1.4](render-system-r1-4.md)

## Objective

Complete the R1 responsibility split by moving source ingestion and scene-state
preparation into one stable Render collaborator, narrowing the public facade,
and replacing Editor's general `GraphicsContext` access with a typed
Graphics-owned presentation bridge.

The linked stage design is authoritative for ownership, API disposition,
threading, failure policy, rejected alternatives, and reference evidence. This
spec records the executable scope and acceptance contract.

## Current state

- `RenderSystem` still owns registry/world/camera/material-resolution details
  and performs their ordered drain directly in `BeginFrame()`.
- Runtime retains source-sink pointers obtained before Render initialization,
  so extraction must preserve their addresses through rollback and shutdown.
- Editor is the only external `GetGraphicsContext()` consumer and casts its
  native pointer to `VulkanContext` only to obtain `VulkanEditorBridge`.
- Editor requests only `RenderTargetView`, although the facade returns the full
  `RenderTarget`.
- `Tick()` is unused and `PostInitialize()` only logs catalog statistics after
  R1.4.
- R1.4's strict cross-backend silhouette comparison is superseded by a reviewed
  policy that separately bounds edge/structural differences, requires matching
  contour bounds, and rejects translated or removed thin geometry.

## Scope

- Add an address-stable `RenderSceneCoordinator` owning source registries,
  Render/Light worlds, camera/environment selection, material-asset records,
  ordered drain, frame-scene input, and scene cleanup.
- Delegate the four existing sink capabilities through the coordinator without
  changing source contracts or handles.
- Reduce `BeginFrame()` to scene preparation, backend/frame setup, renderer and
  capture outcome, and lifecycle state.
- Add failure unwind when a begun backend frame has no usable active frame
  context/recorder.
- Add a common typed editor-presentation bridge implemented by Vulkan/OpenGL
  backend paths and consumed by the API-specific ImGui adapters.
- Remove public `GetGraphicsContext()`, replace full target access with a value
  view, replace scalar shader count with a metrics snapshot, and remove unused
  `Tick()`/empty `PostInitialize()` wiring.
- Update Render, Graphics, Runtime, Editor, tests, CMake, and durable docs needed
  by those contract changes.
- Produce formal review, journal, full validation, and final R1 evidence.

## Non-goals

- Render graph, dynamic pass registration, renderer plugins, or one class per
  pass.
- Shader ABI, target format, lighting, shadow, material, capture, or visual
  feature changes.
- General native API interop or removal of Graphics-internal
  `GraphicsContext` helpers.
- Gameplay/editor inspection snapshots or mutable Editor access to Gameplay.
- RuntimeLib ↔ EditorLib cycle removal.
- Streaming, hot reload, asset eviction, or prepared-catalog redesign.

## Invariants

- Runtime owns one `RenderSystem`; Gameplay sources are destroyed before it.
- Source sink addresses remain stable from facade construction through
  destruction even when initialization fails and retries.
- Only the render thread drains source commands and mutates scene state.
- Render owns scene and renderer policy; Graphics owns GPU/native objects,
  frame execution, synchronization, and the editor-presentation bridge.
- Native ImGui types remain in API-specific Editor/Graphics implementation
  files, never common Render/Graphics headers.
- The scene view and editor bridge are borrowed capabilities with explicit
  resize/shutdown invalidation.
- Fixed pass order, fail-soft pass outcomes, capture behavior, and teardown
  order from R1.1–R1.4 remain unchanged.

## Stages

1. Inventory facade callers and add regression coverage for sink stability,
   failed-init retry, frame-open unwind, view invalidation, and Editor teardown.
2. Extract `RenderSceneCoordinator` and frame-scoped scene input while
   preserving drain order, defaults, source resolution, and cleanup.
3. Introduce the common editor-presentation bridge; migrate Vulkan/OpenGL and
   ImGui adapters; remove public general-context access.
4. Narrow target/metrics access and delete unused `Tick()` and Render
   `PostInitialize()` wiring.
5. Run architecture audits, focused/full tests, both smoke paths, Editor
   lifecycle, and six visual captures; diagnose the outstanding comparator.
6. Complete formal review and the R1.5 journal, then update R1/TODO/status/risk
   ledgers only to the level supported by evidence.

## Acceptance criteria

- [x] The coordinator exclusively owns source inboxes and Render scene state,
  while `RenderSystem` remains composition/frame owner.
- [x] Pre-initialization sink pointers survive success, rollback/retry, and
  shutdown without replacement or stale access.
- [x] Scene preparation produces one frame-scoped immutable input in the
  established drain order.
- [x] A failed active-context/recorder acquisition closes the backend frame and
  leaves lifecycle state `Ready` with no active pointer.
- [x] `Tick()` and the empty Render `PostInitialize()` chain have no remaining
  declaration or caller.
- [x] Editor receives a typed presentation bridge; no Render/Editor caller can
  recover the full backend `GraphicsContext` or `VulkanContext`.
- [x] Render's public scene output is a borrowed value view, not a
  `RenderTarget` or attachment handle.
- [x] Common contracts remain free of Vulkan/OpenGL types and Graphics remains
  free of ImGui policy.
- [ ] Focused and full automated validation, dual-backend smoke, Editor
  startup/shutdown, resize, teardown, and six inspected captures pass. Native
  orderly Editor shutdown remains unverified in the current environment.
- [x] The R1.4 silhouette comparator is resolved or superseded by a reviewed
  evidence policy before R1 is marked complete.
- [ ] Review and journal evidence close every parent R1 criterion or name the
  exact remaining blocker: native orderly application-close evidence.

## Validation plan

Required level: Levels 2–4 from
[`docs/validation_matrix.md`](../../docs/validation_matrix.md), because shared
Render/Graphics contracts and Runtime/Editor wiring change.

- Build and run focused RenderSystem/coordinator, render-pass, source registry,
  Graphics contract, and affected Runtime tests.
- Build affected Runtime and Editor targets.
- Search for removed APIs, public context casts, full target exposure, native
  types in common headers, and forbidden dependency edges.
- Run full Debug build and complete CTest sequentially.
- Run `GraphicsSmoke` for Vulkan and OpenGL, including resize and teardown.
- Launch the Runtime editor path and verify UI initialization/close on both
  APIs.
- Capture and inspect PBR, point-shadow, and spot-shadow SceneColor on both
  APIs; record exact image/comparator commands and results.

## Risks and open questions

- The typed common bridge still leads to an API-specific downcast inside the
  approved Editor adapter. Tests must make mismatch failure deterministic.
- A borrowed native target view is invalidated by resize. The Editor must
  reacquire it every draw and release any backend registration before shutdown.
- Moving material records changes cleanup ownership; tests must preserve
  release-before-backend order.
- The cross-backend comparator must retain its bounded edge/structural metrics
  and synthetic rejection probes whenever the capture fixture changes.
