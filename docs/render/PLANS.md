# Render Module Plans

**Status: active.** This page defines the Render module's architecture and
maps its focused submodule plans. It is not a stage-by-stage implementation
plan; concrete stage designs live in each submodule's `.plan/` directory.

## Module architecture

Render owns the policy of what to draw and how to schedule it. It consumes
value-only source data and processed Resource artifacts, creates API-neutral
pipeline and binding descriptions, and submits them through Graphics/RHI.

```text
Gameplay source values
        ↓
Render source registries and immutable snapshots
        ↓
RenderWorld / materials / cameras / pass schedule
        ↓
API-neutral PipelineDesc, targets, bindings, commands
        ↓
Graphics/RHI GPU resources and backend execution
```

Ownership remains deliberately split:

- Asset owns asset identity, decoding, and CPU-side asset lifetime.
- Resource converts CPU assets into render-ready CPU artifacts and does not
  own GPU objects.
- Render owns scene policy, material interpretation, pass dependencies, frame
  data, and logical target selection.
- Graphics/RHI owns GPU allocation, API translation, synchronization, and safe
  destruction.

The default frame policy is an explicit ordered schedule. A render graph is a
later decision that requires measured dependency, aliasing, or scheduling
pressure.

`RenderSystem` is currently broader than this target: it combines composition,
resource ingestion, scene handoff, deferred-pass implementation, pass-private
GPU state, and teardown. The proposed
[R1 responsibility split](.plan/R1.md) keeps the public facade and fixed
schedule while separating those existing ownership domains.

## Render-wide stage plans

- [R1 — RenderSystem responsibility split](.plan/R1.md) — characterize and
  reduce the current facade/renderer/resource/lifecycle coupling without
  introducing a render graph.
- [R1.2 — deferred renderer and pass-owned state](.plan/R1.2.md) — move the
  current deferred renderer's targets, pass state, environment/shadow policy,
  recording, and cleanup behind one concrete collaborator while preserving the
  R1.1 facade and frame behavior.
- [R1.3 — fixed pass declaration/execution unification](.plan/R1.3.md) — use
  one immutable typed sequence for logical validation, renderer invocation,
  conditional capture conversion, and optional external terminal composition;
  do not introduce graph algorithms.

## Focused submodules

| Submodule | Architecture | Roadmap | Stage plans |
| --- | --- | --- | --- |
| Material System | [PLANS](material_system/PLANS.md) | [TODO](material_system/TODO.md) | [`.plan/`](material_system/.plan/) |
| Deferred PBR | [PLANS](deferred_pbr/PLANS.md) | [TODO](deferred_pbr/TODO.md) | [`.plan/`](deferred_pbr/.plan/) |
| Render Capture | [PLANS](render_capture/PLANS.md) | [TODO](render_capture/TODO.md) | [`.plan/`](render_capture/.plan/) |
| Render Scene | [PLANS](render_scene/PLANS.md) | [TODO](render_scene/TODO.md) | [`.plan/`](render_scene/.plan/) |

## Module references

- [Overview](overview.md) — module entry point and document map.
- [Design](design.md) — ownership, frame policy, and pipeline seam.
- [Lifecycle](lifecycle.md) — initialization, frame work, and teardown.
- [Dependencies](dependencies.md) — allowed dependency directions.
- [Risks](risks.md) — known limits and validation gaps.
- [Usage](usage.md) — runtime capture and validation workflow.

## Layering rule

Read this file for Render-wide architecture. Read a submodule `PLANS.md` for
that submodule's architecture and policy. Read `.plan/<stage>.md` for concrete
stage design. Read the submodule `TODO.md` for current work status. Read the
central `.spec/journal/` entry for what actually happened.
