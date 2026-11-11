# ED6.2 — Neutral Editor UI and backend execution scope

- Status: implementation authorized; execution in progress
- Date: 2026-11-10
- Parent: [ED6 roadmap](../../docs/editor/TODO.md#ed6--reusable-ui-and-editor-reconstruction)
- Canonical stage design: [ED6.2](../../docs/editor/.plan/ED6.2.md)
- Parent execution scope: [Editor reconstruction](editor-ui-reconstruction.md)
- Design review: [ED6.2](../../docs/editor/.review/ED6.2.md)
- Evidence: [planning journal](../journal/2026-11-10-editor-ui-ed6-2.md)

## Objective

Establish a reusable Editor UI API that exposes no ImGui types/headers/context;
implement ImGui behind its private backend boundary; separate UIWidgets,
UIPresentation and backend targets; prove the boundary with a recording backend,
a real Settings property-fields view and an independently linked UI application.

Preserve a path toward AI Native through stable identity, semantic descriptors,
typed results and optional internal frame observations with actual layout
provenance. Domain owners remain authoritative. This stage does not define an
agent protocol, JSON UI schema, runtime-generated layout system or second
production GUI backend.

## Authorization and current state

The user's 2026-11-10 instruction, “ED6.2 detailed plan 根据此方案执行,”
supersedes the planning-only authorization recorded when this scope was drafted.
It authorizes source and CMake changes within the canonical ED6.2 boundaries.
Preserve existing dirty Settings, Log and ED6.1 implementation. Do not claim
acceptance until G1–G8 have execution evidence.

## Implementation boundaries

The design orders ED6.2.0–ED6.2.6: contract freeze, neutral core/recording,
ImGui adapter, presentation lifecycle, real feature pilot, host delegation,
and final acceptance. Its v1 capability list, ownership table and legacy
allowlist are the implementation boundaries; API expansion requires a current
consumer and explicit contract review.

- Feature code uses UiContext and Editor-owned values, not toolkit escape hatches.
- RuntimeLib/EditorLib/SceneEditor are absent from new neutral/backend target
  dependencies; the old aggregate remains a one-way migration consumer.
- Keep ED6.1 hidden update/close decisions and current domain revision/provider
  behavior. Existing snapshots, resource retirement and game/render lanes remain.
- Legacy callbacks are confined to compatibility and remain listed migration
  debt; new callers and the real neutral pilot cannot use that bypass.
- UI image tokens have explicit validity; views do not own GPU resources.
- Semantic observations are optional, bounded, immutable copies and distinguish
  actual versus simulated geometry. They are not another domain-state store.
- No source copies/imports from reference projects or modifications to third_party.

## Acceptance and validation

Canonical design gates G1–G8 cover compile isolation, independent links,
behavior/precision/scope semantics, AI-adjacent observations, lifecycle failures,
image/Graphics correctness, enabled-host matrices, dependency/performance review.
All must have actual evidence before accepting ED6.2. A recording-only test does
not prove production pixels, another GUI implementation or GPU safety.

Implementation changes shared APIs and CMake boundaries: Level 4 is required
with targeted inner-loop tests, then visible Debug Vulkan validation and OpenGL
checks through the external Default-desktop path, plus matched rebuilt
RelWithDebInfo timing. The ED6.0 instantaneous sample is not a matched p95
baseline; establish repeatable samples before source changes.

The original planning authorization was superseded by the user's explicit
execution request. The implementation has begun, but the required Level 4
acceptance matrix remains open; see the execution journal for actual results and
unverified gates.

## Remaining work

The full feature/workspace toolkit migration remains ED6.5 and final enforcement
ED6.7; Settings feature ownership acceptance remains ED6.4. Existing ED5/SET3
acceptance and known failures stay independent. Exact C++ declarations are
frozen at ED6.2.0 within the documented semantics, before implementation grows.
