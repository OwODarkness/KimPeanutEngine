# Render Module TODO

**Status: active.** This is the Render-level roadmap. Submodule implementation
details and stage checklists belong in the linked submodule documents.

## Submodule roadmaps

- [Material System](material_system/TODO.md) — render-owned material templates,
  instances, asset resolution, and frame-local bindings.
- [Deferred PBR](deferred_pbr/TODO.md) — attachment contract, G-buffer,
  lighting, shadows, environment, and presentation.
- [Render Capture](render_capture/TODO.md) — semantic capture requests,
  conversion views, readback, and screenshot export.
- [Render Scene](render_scene/TODO.md) — common scene-recording boundary and
  removal of legacy scene seams.

## Cross-cutting Render work

- [ ] **R1 — RenderSystem responsibility split:** follow the
  [stage design](.plan/R1.md) and [risk register](risks.md). Characterize real
  frame/lifecycle behavior first; then make lifecycle transactional, extract
  deferred renderer/pass-owned state, unify fixed-pass declaration with actual
  execution, and remove synchronous Asset/resource loading work from Render.
- [x] **R1.1 — characterization and transactional lifecycle** landed
  2026-09-01: injected the existing `RenderBackend` factory, added explicit
  `Uninitialized`/`Ready`/`FrameActive`/`ShutDown` states, transactional
  initialization with diagnostics, reverse cleanup, and idempotent shutdown.
  `RenderSystemTest` records the current pass/frame, capture, resize, editor,
  rollback, and teardown behavior.
- [x] **R1.2 — deferred renderer and pass-owned state:** implementation is
  landed in the [stage design](.plan/R1.2.md) shape; all four findings in the
  [formal review](.review/R1.2.md) are addressed, and focused/full build,
  smoke, and fresh Vulkan/OpenGL capture evidence passed. The direct renderer
  owns named targets, pass-private handles, environment/shadow state, pass
  recording, and cleanup; R1.3 sequence unification and R1.4 ingestion cleanup
  remain separate stages. → [R1.2 spec](../../.spec/specs/render-system-r1-2.md), [R1.2 journal](../../.spec/journal/2026-09-02-render-system-r1-2.md)
- [x] **R1.3 — fixed pass declaration/execution unification:** implementation
  landed 2026-09-02; the [formal review](.review/R1.3.md) fixes canonical
  typed-ID role binding, moved-from cursor reuse, and stale module records.
  Focused/full build, CTest, dual-backend GraphicsSmoke, and fresh Runtime
  startup-level captures pass. The landed
  [stage design](.plan/R1.3.md) and [execution spec](../../.spec/specs/render-system-r1-3.md)
  now correspond to one immutable typed sequence, conditional diagnostic
  capture, and optional terminal editor composition. Render-graph and R1.4
  work remain out of scope. → [R1.3 journal](../../.spec/journal/2026-09-02-render-system-r1-3.md)
- [x] Add direct RenderSystem orchestration tests for partial-init rollback,
  fixed pass order, conditional capture, resize, terminal editor composition,
  and reverse-order teardown before moving the corresponding code (R1.1).
- [ ] Keep source registries, immutable snapshots, pass scheduling, and
  frame-local resource lifetime aligned across Render submodules.
- [ ] Add read-only Gameplay/editor snapshots before exposing mutable gameplay
  state to editor tools.
- [ ] Revisit the explicit pass schedule only after measured dependency,
  aliasing, or scheduling pressure exists.
- [ ] Keep Render documentation links, validation evidence, and ownership
  boundaries current when a submodule lands work.

## Document ownership

This file tracks only Render-wide work. Use each submodule's `TODO.md` for
subtask status, `.plan/` for concrete stage design, and `.spec/journal/` for
dated implementation evidence.
