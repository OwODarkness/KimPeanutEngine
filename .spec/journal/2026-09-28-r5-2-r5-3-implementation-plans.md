# R5.2 / R5.3 implementation planning — 2026-09-28

## Request and inspected state

User requested concrete implementation plans for the two extraction stages,
then clarified that they should make full use of the render graph. This turn
changes documentation only; no renderer implementation or new acceptance claim.

Inspected HEAD `01d2242caac1ffafcc4c600d54d0b0fc129e9029` plus existing R5.1
working-tree changes across Render, Graphics, Editor, tests and documentation.
`deferred_renderer.cpp` remains 4,587 lines. Existing changes were preserved.
Read the current facade header/source, fixed declaration, graph frame cursor,
target owner, common recorder/RT owner, submission executor and stage/spec
contracts. The concrete plans use current symbol names, not assumed line ranges.

## Decisions and documents

- [R5.2](../../docs/render/.plan/R5.2.md): five slices for typed declaration roles,
  complete physical binding groups/Graphics-owned build inputs, lease/transition
  extraction, typed pass context and execution/terminal/finalization extraction.
  Existing `RenderGraphFrame` remains the scheduler/outcome cursor. No name-based
  geometry/BLAS exceptions or pretend instance/scratch handles are permitted.
- [R5.3](../../docs/render/.plan/R5.3.md): seven slices mapping current methods,
  fields, structs and constants to shadow, deferred/environment, tone-map,
  capture, RT scene and PT owners; two justified draw/fullscreen helpers avoid
  duplicate caches/resources. Cross-owner values are readonly frame views.
- The graph is authoritative for scheduling/dependencies, transitions,
  attachment brackets, transient lifetime and failure propagation. Owners keep
  persistent state and record through declared typed resources; they cannot
  create parallel schedules or mutate sibling state.
- Preserve R5.1 independent Capture/Viewer outputs, cached-shadow no-clear,
  current area-light/material behavior, history commit semantics and static
  RT table/descriptor reuse. No shader optimization, queue framework or GPU
  retirement redesign is included. R5.0 image/graph gates remain prerequisites.
- Facade reduction is structural acceptance. The R5.3 800–1,200-line target is
  a planning estimate, not an implemented result or justification for file-only
  splitting. Numeric performance/image acceptance follows the existing spec.

Updated parent R5 plan, Render PLANS/TODO, execution-spec stage links and project
status to index the new documents. Used modular-documentation conventions.
Used the existing pinned Sakura source study for graph phase boundaries; fresh
pinned frontend fetch failed (cache miss), and source-search connector was
unavailable. This limitation is recorded in R5.2; no new upstream claim is made.

## Validation

Documentation-only level 0: inspect plan consistency, check local Markdown
targets/anchors and `git diff --check`. No build, tests, engine launch or
performance sampling is warranted for these Markdown changes. New-document
local links/anchors and parent links to both plans pass. A wider scan of 521
links found five preexisting anchor mismatches in Render TODO/project status
(R4.7/AP1 references), outside this task; they were not rewritten. `git diff
--check` passes with line-ending notices in the shared working tree.
R5.2/R5.3 remain planned and unchecked in the roadmap.
