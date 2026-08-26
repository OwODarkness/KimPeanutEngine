# gkNextEngine — Rendering and Runtime Reference

**Study snapshot: 2026-08-26, `gameknife/gkNextEngine` branch `main` (tip, not a tagged release).** Learn from the design; do not copy source or assume its constraints fit KimPeanutEngine.

## Core direction

gkNextEngine is a Vulkan-first C++20 engine and rendering playground aimed at practical real-time rendering rather than an isolated graphics demo. Its public design combines path tracing, hybrid rendering, GPU-driven rasterization, editor tooling, asset import, scripting, physics, gameplay prototypes, benchmarks, and visual validation in one runtime workflow.

## What it demonstrates

- **Renderer-first runtime:** path tracing and raster/hybrid pipelines share scene and asset data so different renderers can be compared without rebuilding the content path.
- **Modern GPU submission:** visibility-buffer, bindless, GPU-driven, and multi-draw techniques reduce CPU submission work for large scenes.
- **Complete engine integration:** ECS/reflection, editor tools, scripting, physics, animation, and asset import are connected to the renderer instead of existing as unrelated samples.
- **Structured content pipeline:** glTF and other structured formats become runtime scene/material/animation data rather than only imported meshes.
- **Evidence-driven iteration:** benchmarks, per-pass profiling, screenshots, and visual tests make rendering changes measurable.
- **Controlled scope:** the project explicitly values a relatively small and readable engine core over a large general-purpose framework.

## What transfers to KimPeanutEngine

1. **Use one scene representation across renderers.** Keep scene/resource ownership above the backend so Vulkan and OpenGL execute the same logical render data. This supports the current `RenderWorld`, `RenderScene`, and `CommandRecorder` direction.
2. **Separate renderer policy from device execution.** A renderer should decide visibility, passes, materials, and pipeline descriptions; the RHI/device should own GPU resources, queues, synchronization, and API translation. This is the next boundary to improve around `RenderSystem` and `RenderBackend`.
3. **Make the pipeline measurable.** Add a headless smoke target, screenshot/visual regression target, and benchmark report alongside the current unit tests and `GraphicsSmoke` executable.
4. **Prefer capability-driven modern features.** Treat bindless, GPU-driven submission, visibility buffers, and ray tracing as renderer capabilities—not mandatory concepts in the common RHI. OpenGL should not force the lowest common denominator into the Vulkan path.
5. **Keep content structured.** Extend the Asset → Resource → GPU path toward materials, animation, scene metadata, and import diagnostics rather than stopping at mesh/texture upload.
6. **Keep the core readable.** Add a feature only when it has a concrete runtime, editor, or validation consumer; avoid creating framework layers that have no current data flow.

## Traps to avoid

- Do not copy gkNextEngine's Vulkan-only assumptions into KimPeanutEngine's cross-API RHI.
- Do not add bindless or GPU-driven APIs before the renderer has a workload that benefits from them.
- Do not let `RenderSystem` become the owner of every runtime concern merely because it is the current integration point.
- Do not compare feature checklists without matching validation evidence and workloads.
- The public repository describes a moving development branch, not a stable release contract.

## Sources

- Repository: `gameknife/gkNextEngine`, branch `main`
- `README.en.md` — project goals, rendering direction, runtime/editor systems, benchmarks, and validation
- `src/Runtime`, `src/Renderer`, `src/Editor` — inspect the current implementation when a specific design question is being studied
- Related overview: [engine-reference index](../engine-reference/README.md)
