# Architecture Overview

KimPeanut Engine is organized around explicit ownership and dependency
direction. The main runtime flow is:

```text
Editor → Runtime → Asset / Resource → Render → Graphics/RHI
                                              ├── OpenGL
                                              └── Vulkan
```

## Layers

| Layer | Responsibility |
| --- | --- |
| Editor | ImGui tools, inspectors, editor state, and presentation integration |
| Runtime | Engine lifecycle, input, windowing, gameplay coordination, and scheduling |
| Asset | Asset identity, loading, caching, dependencies, and CPU-side lifetime |
| Resource | CPU resource conversion, validation, and reusable processed data |
| Render | Scene policy, materials, render passes, frame data, and render snapshots |
| Graphics/RHI | API-neutral GPU resources, commands, synchronization, and handles |
| Backend | OpenGL/Vulkan translation and native API execution |

## Data flow

```text
Asset file
  → AssetManager
  → Resource processing
  → Render resource / pipeline description
  → RHI resource handle
  → FrameContext + command recording
  → OpenGL or Vulkan submission
```

Asset loading and resource processing happen before render submission. Render
consumes validated, CPU-side data and produces frame-local work. Graphics/RHI
owns the GPU-facing objects and the synchronization required to use and retire
them safely.

## Ownership boundaries

- Asset owns asset identity, loading, cache records, dependency edges, and
  CPU-side asset lifetime.
- Resource processing converts CPU asset data into render-ready artifacts. It
  does not own GPU objects.
- Render owns scene policy, materials, pass scheduling, pipeline descriptions,
  and frame-local render data.
- Graphics/RHI owns GPU resources, API execution, synchronization, and backend
  translation.
- Runtime does not depend on Editor for its core operation.
- Common Render and Graphics/RHI interfaces do not expose Vulkan or OpenGL
  implementation types.

## Module map

```text
engine/runtime/core       Common types, math, configuration, logging, and utilities
engine/runtime/asset       Asset loading, products, import, and dependencies
engine/runtime/graphics    Graphics contracts and OpenGL/Vulkan backends
engine/runtime/render      Render policy, passes, materials, and frame data
engine/runtime/gameplay    Runtime world, actors, components, and source bridges
engine/editor              Editor shell, UI components, and tools
engine/module              Optional modules such as TTS and Live2D
engine/test/unit            GoogleTest unit and contract tests
```

## Design principles

- Prefer explicit ownership over hidden global state.
- Keep common contracts independent of backend-specific types.
- Apply state changes at clear frame or transaction boundaries.
- Validate handles, descriptors, paths, and product bytes before publication.
- Keep implementation abstractions tied to a concrete consumer and data flow.

Module-specific architecture and current work live beside each module. The
[project status](status.md) summarizes the current implementation state.
