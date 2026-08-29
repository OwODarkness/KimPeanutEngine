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

`RenderSystem` owns a small explicit ordered pass schedule rather than a general
render graph. Scene passes write the logical `SceneColor`; the terminal editor
composite consumes it after input polling. Logical resource names and pass
dependencies remain Render vocabulary and do not expose `Vk*`, OpenGL, queues,
or command buffers.

`RenderWorld` resolves Gameplay-produced value-only source records into private
`MeshProxy` state. Gameplay cannot hold `MeshProxy`, material, RHI, or backend
types.

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
