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
  execution, and remove synchronous Asset/bootstrap path work from Render.
- [x] **R1.1 — characterization and transactional lifecycle** landed
  2026-09-01: injected the existing `RenderBackend` factory, added explicit
  `Uninitialized`/`Ready`/`FrameActive`/`ShutDown` states, transactional
  initialization with diagnostics, reverse cleanup, and idempotent shutdown.
  `RenderSystemTest` records the current pass/frame, capture, resize, editor,
  rollback, and teardown behavior.
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
