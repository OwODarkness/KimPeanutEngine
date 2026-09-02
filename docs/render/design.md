# Render Design

## Responsibility boundary

Render owns scene policy: `RenderSystem`, `RenderWorld`, render passes,
materials, cameras, render-target choice, and render-ready descriptions.
Graphics/RHI consumes those descriptions and owns native API objects, command
encoding, synchronization, and GPU destruction safety.

| Concern | Owner |
|---|---|
| Asset identity and CPU-side lifetime | Asset |
| Derived shader/resource artifacts | Resource pipeline |
| Pipeline/material/pass/scene policy | Render |
| GPU resources and backend execution | Graphics/RHI |

## Frame policy

The current renderer uses one immutable, explicitly ordered
`FixedRenderPassSequence` rather than a general render graph. Each closed typed
pass ID carries its logical resources, execution owner, condition, and terminal
policy; static validation binds the ID to its canonical ordinal. A short-lived
`FixedRenderPassFrame` visits that same sequence and records execution, skip,
and failure outcomes. Scene passes write the logical `SceneColor`; the
terminal editor composite consumes it after input polling. Logical resource
names and pass dependencies remain Render vocabulary and do not expose `Vk*`,
OpenGL, queues, or command buffers.

`DeferredRenderer::ExecutePass()` maps each renderer-owned ID to one recording
operation, while the frame cursor gates conditional capture and the optional
external editor terminal. Renderer failure remains fail-soft for the frame,
but is observable in the per-pass outcomes. This keeps declaration and
execution on one closed contract without introducing render-graph compilation,
resource aliasing, or synchronization inference.

`RenderWorld` resolves Gameplay-produced value-only source records into private
`MeshProxy` state. Gameplay cannot hold `MeshProxy`, material, RHI, or backend
types.

## RenderSystem target shape

`RenderSystem` is the Runtime-facing facade and composition root, not the
permanent home of every renderer concern. It owns the frame bracket and wires
focused collaborators. The deferred renderer owns pass policy and pass-private
state; Runtime owns startup asset preparation; Graphics owns GPU execution.

```text
RuntimeContext → RenderSystem facade
                    ├─ source registries / RenderWorld snapshots
                    ├─ immutable prepared asset/artifact catalog
                    ├─ DeferredRenderer → fixed pass sequence + pass state
                    └─ RenderBackend / FrameContext frame bracket
```

R1.4's landed ingestion boundary is detailed in the
[ready-asset stage plan](.plan/R1.4.md). Runtime prepares the selected level's
renderable dependency closure and closed renderer built-ins through Asset and
Resource before Render initialization. Render receives no paths and performs
only catalog lookup, render-policy interpretation, and GPU-resource resolution.

Dependencies are explicit. Do not replace `RenderSystem` with a large context
bag, service locator, broad virtual renderer interface, or speculative
class-per-pass hierarchy.

## Pipeline seam

Render builds a valid `graphics::PipelineDesc` from asset identity, processed
shader artifacts, material state, vertex layout, descriptor bindings, and
attachment formats. The RHI validates/bakes that description but never loads
files, compiles source, or decides pipeline policy.

```text
Asset shader identity → Resource processing → Render PipelineDesc → Graphics GPU pipeline
```

## Non-goals

- Do not make Render load arbitrary source files outside Asset/Resource.
- Do not put backend-specific implementation types in common Render contracts.
- Do not introduce a general render graph until a concrete pass/resource need
  exceeds the current explicit schedule.
- Do not treat smaller files or a lower line count as completion; ownership,
  lifecycle, single-source pass order, and testability are the criteria.
