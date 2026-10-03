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

The current frame policy is the compiled R3 render graph. R4 is the proposed
consumer-driven extension for ray tracing: it first closes the clean-baseline
and graph-correctness gates, then adds the minimum Graphics and graph contracts
required by one selected RT consumer.

R1.2–R1.5 removed deferred-pass implementation, duplicated pass order,
Asset/Resource preparation, and source/scene ownership from `RenderSystem`.
The facade now combines only composition/frame lifetime, focused collaborators,
and the typed Editor presentation capability. The fixed schedule remains the
public policy while the coordinator owns scene preparation.

## Render-wide stage plans

- [R7 — Raster image quality](.plan/R7.md) — planned spatial SSAO,
  motion/history contracts, native-resolution TAA, and exponential height fog;
  closes affected R5 gates first and keeps R6 PT reconstruction separate.
  Reflection probes, SSR, and volumetric fog remain follow-up decision gates.

- [R6 — Interactive path tracing and temporal reconstruction](.plan/R6.md) —
  active low-SPP moving-camera denoising, multi-light GPU attribution and
  evidence-gated optimization; retains progressive Beauty and R5 correctness
  prerequisites. Includes Nsight findings and conditional experiments selected
  by measured GPU cost.

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
- [R1.4 — ready asset ingestion and Render bootstrap removal](.plan/R1.4.md) —
  prepare one immutable CPU asset/artifact catalog before Render initialization,
  remove Render-side loading/processing and the unused path queue, and preserve
  current GPU cache ownership.
- [R1.5 — facade hardening and R1 evidence](.plan/R1.5.md) — move source/scene
  preparation behind one stable coordinator, replace the raw Editor graphics
  context with a typed presentation bridge, narrow target/metrics access, and
  close R1 with cross-backend evidence.
- [R2 — adaptive render spatial index](.plan/R2.md) — one renderer-facing
  `RenderSpatialIndex` boundary with `Flat` / `Bvh` / `Auto` strategies and
  workload instrumentation, so where visibility culling happens and what
  structure backs it can change without touching pass policy. **Design only, not
  authorized for implementation.**
- [R3 — render graph foundation](.plan/R3.md) — evolve the fixed declaration
  into a Render-owned compiled dependency graph, prove raster parity, then add a
  portable resource-state plan and Graphics-owned non-aliasing transients.
  **R3 complete and closed (2026-09-18); the compiled plan is the schedule and
  R3.7's extensions closed as a gate.**
- [R4 — ray-tracing foundation and render-graph integration](.plan/R4.md) —
  selected ray-query directional hard-shadow visibility and closed the
  validation-clean raster gate; R4.1 hardened graph write/binding/transition
  semantics, R4.2 defines the API-neutral Graphics RT contract, R4.3–R4.5 bring
  the imported/scheduled TLAS into hybrid lighting, and R4.6 attempts a dedicated
  Cornell Box RT-pipeline validation with area-light sampling, diffuse indirect
  lighting, and accumulation. See the [R4.6 stage plan](.plan/R4.6.md) and
  [R4.6 spec](../../.spec/specs/render-r4-6-cornell-path-tracing.md).
  **R4.5 evidence remains open; Cornell fixture acceptance is recorded, while
  general scene shading remains open.** The
  [scene-driven RT follow-up](.plan/R4.6-general-scene.md) defines common
  geometry/instance/section/material/light inputs and optional execution policy;
  [R4.6 review](.review/R4.6.md) records the live Sponza baseline.
- [R4.7 — general path-tracing optimization](.plan/R4.7.md), with its
  [GPU strategy extension](.plan/R4.7-gpu.md) — proposed GPU
  visibility/primary-hit/AS improvements and CPU table/binding/preparation
  reuse at unchanged scene-independent lighting quality. Optional budget
  settings are measured separately. See the [source review](.review/R4.7.md)
  and [acceptance spec](../../.spec/specs/render-r4-7-path-tracing-performance.md).
- [R5 — Render configuration, graph execution, and ownership](.plan/R5.md) —
  refreshed against the current general-scene PT renderer: independent settings,
  consumer demand and failure semantics precede typed graph execution and
  pass-family extraction. Preserve R4.7 scene/binding reuse and Graphics
  retirement. GPU tuning is paused. Concrete stage designs: [R5.0 baseline
  freeze](.plan/R5.0.md) and [R5.1 settings, demand and failure](.plan/R5.1.md).
  The
  [dated source recheck](.review/R5.md#baseline-refresh--2026-09-27) records
  current evidence and remaining baseline gates.
- [issue-9.7 — Sponza quality and throughput](.plan/issue-9.7.md) — correct
  texture minification and bound texture, descriptor, visibility, and static-
  shadow costs through Resource, Render, and Graphics ownership boundaries.

## Focused submodules

| Submodule | Architecture | Roadmap | Stage plans |
| --- | --- | --- | --- |
| Material System | [PLANS](material_system/PLANS.md) | [TODO](material_system/TODO.md) | [`.plan/`](material_system/.plan/) |
| Deferred PBR | [PLANS](deferred_pbr/PLANS.md) | [TODO](deferred_pbr/TODO.md) | [`.plan/`](deferred_pbr/.plan/) |
| Render Capture | [PLANS](render_capture/PLANS.md) | [TODO](render_capture/TODO.md) | [`.plan/`](render_capture/.plan/) |
| Render Scene | [PLANS](render_scene/PLANS.md) | [TODO](render_scene/TODO.md) | [`.plan/`](render_scene/.plan/) |
| Render Graph | [PLANS](render_graph/PLANS.md) | [TODO](render_graph/TODO.md) | [R3](.plan/R3.md), [R4](.plan/R4.md) |

## Module references

- [Overview](overview.md) — module entry point and document map.
- [Design](design.md) — ownership, frame policy, and pipeline seam.
- [Lifecycle](lifecycle.md) — initialization, frame work, and teardown.
- [Dependencies](dependencies.md) — allowed dependency directions.
- [Risks](risks.md) — known limits and validation gaps.
- [Usage](usage.md) — runtime capture and validation workflow.

## Graph execution and pass ownership

The concrete facade reduction work is defined by [R5.2](.plan/R5.2.md) and
[R5.3](.plan/R5.3.md): graph-owned execution and typed resources, followed by
cohesive pass-family methods/state/struct extraction. The graph remains the
single dependency/execution authority; pass owners retain persistent caches and
record through typed graph context.

## Rectangular PT emitters

Scene-authored rectangular PT emitters reuse the local-light source lifecycle
with copied half axes. Render packs a finite emitter into the existing PT
light record and samples its surface per path sample/bounce; Graphics retains
GPU ownership. Punctual-only scenes retain cached primary direct lighting.
Raster currently uses the center-point approximation. The authoring and
transport limits are documented in [usage](usage.md#rectangular-path-tracing-light).

## Layering rule

Read this file for Render-wide architecture. Read a submodule `PLANS.md` for
that submodule's architecture and policy. Read `.plan/<stage>.md` for concrete
stage design. Read the submodule `TODO.md` for current work status. Read the
central `.spec/journal/` entry for what actually happened.
