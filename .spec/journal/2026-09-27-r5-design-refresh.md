# R5 design refresh — 2026-09-27

## Request and baseline

Update the original post-R4.6 R5 plan for today's renderer and add actionable
implementation detail. GPU optimization is paused; no source work requested.
Baseline commit: `9089df26325b5976b0ab2bc20bde46febd2ac9a2`. Existing uncommitted
R4.7 documentation and investigation journal were preserved.

No Render R4.8 stage record was found in `docs/` or `.spec/`. The refreshed plan
uses exact current source/evidence and leaves stage-label reconciliation to
R5.0. It does not declare R4.7/R4.8 accepted or mark historical gates complete.

## Changes and evidence

Read the module guides, original R5 plan/review, current roadmaps/status,
validation matrix and the existing Sakura source study. Rechecked pass
conditional failure, physical resource-name binding, eager graph variants,
Graphics uniform/timing schema, direct Vulkan pipeline destruction, Editor
diagnostic default and probe/settings coupling. The dated
[review addendum](../../docs/render/.review/R5.md#baseline-refresh--2026-09-27)
records evidence and limits without replacing the September 22 review.

Rewrote the canonical [plan](../../docs/render/.plan/R5.md), retaining R5.0–R5.6
IDs and decoupling objectives. Added configuration/legacy migration, consumer
demand release, dependency failure behavior, cache/history invariants, typed
bindings, owner extraction steps and per-stage acceptance. Updated Render
PLANS/TODO, Graphics TODO and project status. No ownership or API was changed.
An execution spec with measured budgets is required before source implementation;
this journal is not that spec.

## Validation and limits

Documentation-only validation: relative Markdown target/anchor checks on the
introduced links, source location checks, final diff inspection and
`git diff --check`. No C++ build, tests, engine launch or new timing/visual capture
was needed or performed. Existing R4 quality, fallback and lifetime gaps remain
open; combined independent settings need fresh correctness/runtime evidence.
