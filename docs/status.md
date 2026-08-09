# Project Status

**Snapshot: 2026-08-09.** This is the agent's source of truth for *what state the world is in* — update it as work lands so a future session doesn't re-derive it. Per-module detail lives in the module docs ([asset](asset/asset_module.md), [graphics](graphics/graphics_module.md), [render](render/render_module.md), [resource](resource/resource_module.md)); this page is the one-line-per-item index.

## Done

- **Asset module** — two-tier ownership (unique_ptr wrappers, ref-counted payloads), thread-safe (load → state mutex order), content-addressed `path_index`. Refactor complete. → [asset_module.md](asset/asset_module.md)
- **Shader identity + artifact pipeline** — `ShaderProgramLoader` (`.shader` meta → per-stage `ShaderResource`), `ShaderProcessor` + `SPIRVCompiler` (GLSL → SPIR-V, content-addressed cache), `PreprocessOperation` (GLSL → preprocessed source, no cache). Per-API artifact via `ShaderProcessor::keep_source_` → `ShaderData` `byte_code` (Vulkan) or `source` (OpenGL). Wired end-to-end by the asset example; the render module is not — see below.
- **RHI** — Vulkan + OpenGL backends behind `RenderBackend::CreateGraphicsBackEnd`; cross-API handles, `PipelineDesc`, `TextureManager`/`MeshManager`/`SamplerManager`. → [graphics_module.md](graphics/graphics_module.md)
- **Audio + TTS modules** — miniaudio system, buffer player, GPT-SoVITS client.
- **Unit tests** — math (vector/matrix) + audio decode.
- **Module design docs** — asset, graphics, render, resource written. → [resource_module.md](resource/resource_module.md) (CPU-side processing layer: compiles/bakes, does not load or touch GPU).

## In progress / built but not wired

- **`ResourcePipeline::ProcessShader` has a caller, but only on the asset end** — the `CompileShader()` example bakes `simple_triangle` GLSL → SPIR-V through the pipeline (verified: fresh compile + cache hit); the **graphics end is still orphaned** — backends don't consume `ShaderData`.
- **Graphics backends still load prebuilt `.spv`/`.vert` by path** — they do not consume `ShaderData` from the resource pipeline; the path-keyed `ShaderManager` still owns shader creation inside the RHI.
- **Render module is legacy, OpenGL-hardcoded** — the whole `engine/runtime/render/` tree predates the RHI and does not link `Graphics`.

## Planned (next up)

1. **Render module reconstruction** — the 7-step plan in [render_module.md](render/render_module.md): wire the resource pipeline in, move `PipelineDesc` construction out of the backend, add the warmup pass, retire the legacy GL path.
2. **RHI leak fixes** — retire path-keyed `ShaderManager`, back `PipelineDesc` shaders with `ShaderData`, remove the build-time `glslc` → `.spv` step.
3. **Resource pipeline gaps** — add `CompileFailed` status (carry error text); make `ProcessShader` take the whole `ShaderProgramResource` as one compile unit.
4. **Headless unit tests** — asset manager, `GenerateShaderHash`, `ShaderCache`, `HandleSystem` are all testable without a GPU.

## Known broken / known issues

- **Stale shader modules** — `opengl_shader_module`/`vulkan_shader_module` reference `shader->glsl`/`shader->spirv` (don't exist on `ShaderData`); **not in the build**. → [dead_code.md](dead_code.md)
- **`glslc` is a hard build requirement** — `Graphics/CMakeLists.txt` fatals if `glslc` isn't findable; it also bakes a hardcoded `.vert/.frag` → `.spv` step.
- **Two disconnected render paths** — `RuntimeLib` links `Graphics` (PUBLIC) and `Render` (PRIVATE), but `Render` itself never links `Graphics`; the legacy renderer and the RHI are separate worlds.
- **`main.cpp` selects examples by uncommenting** — most examples block (windows, `while(1)`); running the binary from an agent shell will hang.

## Dead code & stale paths

→ [docs/dead_code.md](dead_code.md). Everything that is not in the build, not wired, or slated for retirement lives there so it doesn't get "fixed" as if live.
