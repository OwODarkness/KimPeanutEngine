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

The current renderer uses an explicit ordered pass schedule rather than a
general render graph. Scene passes write the logical `SceneColor`; the terminal
editor composite consumes it after input polling. Logical resource names and
pass dependencies remain Render vocabulary and do not expose `Vk*`, OpenGL,
queues, or command buffers.

Today `RenderPassSchedule` validates declarations, while
`RenderSystem::BeginFrame()` independently invokes the pass-recording methods.
That duplicated ordering is transitional and can drift. The
[R1 responsibility split](.plan/R1.md) makes one fixed sequence own both each
pass declaration and its invocation before any render-graph decision.

`RenderWorld` resolves Gameplay-produced value-only source records into private
`MeshProxy` state. Gameplay cannot hold `MeshProxy`, material, RHI, or backend
types.

## RenderSystem target shape

`RenderSystem` is the Runtime-facing facade and composition root, not the
permanent home of every renderer concern. It owns the frame bracket and wires
focused collaborators. The deferred renderer owns pass policy and pass-private
state; resource ingestion owns queue/cache transitions; Graphics owns GPU
execution.

```text
RuntimeContext → RenderSystem facade
                    ├─ source registries / RenderWorld snapshots
                    ├─ render-resource ingestion
                    ├─ DeferredRenderer → fixed pass sequence + pass state
                    └─ RenderBackend / FrameContext frame bracket
```

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
