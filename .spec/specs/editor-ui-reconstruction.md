# Editor UI reconstruction execution scope

- Status: ED6.0 baseline frozen; ED6.1 accepted; ED6.2 onward open
- Date: 2026-11-10
- Parent: [Editor ED6 roadmap](../../docs/editor/TODO.md#ed6--reusable-ui-and-editor-reconstruction)
- Canonical design and stage acceptance: [ED6](../../docs/editor/.plan/ED6.md)
- Baseline findings: [architecture review](../../docs/editor/.review/ED6.md),
  [ED6.0 freeze](../../docs/editor/.review/ED6.0.md)
- Evidence: [journal](../journal/2026-11-10-editor-ui-reconstruction.md)

## Objective

Separate reusable UI presentation/components from workspace hosting and Scene
Editor composition. Feature modules own their editor tools under separate UI
targets, and Runtime has no concrete Editor dependency. Demonstrate reuse with
the existing standalone applications rather than a hypothetical future consumer.

## Current authorized stage

The original planning-only scope was superseded by the user's follow-up
authorization on 2026-11-10 to execute ED6.1, followed by authorization to
apply the revised README/UI boundary and complete ED6.0. ED6.1 source changes
are accepted; ED6.0 baseline documentation is now frozen. The user's follow-up
on 2026-11-11 authorizes the focused shared 2D Plot capability described below;
it does not authorize the remaining ED6.2+ target extraction or a full Signal Lab
UI migration.

ED6.1 source scope already executed:

- Add a small content lifecycle shared by docked and floating hosts.
- Move optional layout participation off the generic UI component base.
- Route hidden updates and accepted/deferred/cancelled close requests through
  the shared host/content contract.
- Keep compatibility wrappers for existing callers and add ImGui-free model
  coverage plus focused host lifecycle coverage.

Do not begin ED6.2 target extraction or other ED6 stages under this scope.
The [ED6.0 review](../../docs/editor/.review/ED6.0.md) records the current
dependency/owner/layout/command inventories, Debug correctness suite, and
RelWithDebInfo UI build/submit sample. Later multi-host and matched final
performance/visual acceptance remain open.

The original follow-up ED6.2 request authorized detailed design only. Its
[stage scope](editor-ui-ed6-2.md) and
[canonical plan](../../docs/editor/.plan/ED6.2.md) define the neutral API/backend
boundary and AI-adjacent observations. The separate 2026-11-11 user follow-up
authorizes this bounded implementation:

- Add a toolkit-neutral 2D Plot API over Editor Canvas, with axes, explicit
  linear/log scales, line/bar/point series, module-supplied styles and pointer
  value results.
- Use the same Plot in Audio's spectrum and Signal Lab's scope/spectrum views.
- Leave audio playback semantics, radial scope drawing and remaining legacy
  Signal Lab controls outside this focused change.

## Invariants for future implementation

- Runtime feature targets remain independent of their editor targets and ImGui.
- Generic UI has no concrete scene/feature model or service dependency.
- ED6.2 onward establishes Editor's toolkit-neutral public UI API; feature,
  SceneEditor and workspace drawing migrates through it, with ImGui confined
  to backend implementation/tests. This is a planned acceptance constraint and
  does not extend the current ED6.1-only implementation authorization.
- Workspace owns placement/visibility and generic host lifetime; feature sessions
  own domain-specific UI state. Views do not own native GPU objects.
- Existing snapshot, edit, save, thread, loading/promotion and shutdown semantics
  survive migration. Pending requests never become success merely by queuing.
- Preserve stable CLI/commands, panel IDs, previous layout formats, optional
  modules, standalone tools, unrelated changes, and all vendored sources.
- ED5/SET3 unresolved acceptance remains open independently of reconstruction.

## Stages and evidence

ED6.0–ED6.7 in the canonical design define the ordered implementation boundary,
chosen owners, migration map, stage acceptance, reference comparison and
validation. Record each stage's actual source state, placement evidence,
commands, results, skipped checks and risks in its implementation journal.
Do not duplicate or pre-complete that ledger here.

## Current completion boundary

ED6.0 is accepted as a frozen baseline after recording current target/toolkit
edges, live owners/lifetimes, layout and command formats, Debug correctness tests,
and RelWithDebInfo UI timings. ED6.1 is accepted after the shared lifecycle and
layout-participant split, headless host coverage, Debug build/tests, and visible
rebuilt Editor check. The broad toolkit-neutral API and target split remain
unimplemented; ED6.2 through ED6.7 remain open. Overall completion still requires independent
UI/runtime link gates, real multi-host reuse, compatible lifecycle/persistence,
and final runtime acceptance described in ED6.
