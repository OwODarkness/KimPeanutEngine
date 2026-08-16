# Resource Module Design (the CPU-side processing layer)

Location: `engine/runtime/core/resource/`

The resource module is the engine's CPU-side **processing** layer — the middle step of *Asset → Process → GPU*. It takes CPU-side data (shader source, asset payloads) and transforms it into a ready artifact (compiled bytes). It does **not** load or unload anything — lifecycle is the asset module's job. It does **not** touch the GPU — device objects are the RHI's job. Its one job is to **process**: the prime example is shader compile (GLSL source → SPIR-V bytes).

## The role — what it is and what it is not

The three-way split that defines this module:

| Concern | Owner |
|---|---|
| Load / unload / dedup / lifetime / dependencies | **asset** (identity) |
| **Process: CPU-side data → artifact** (compile, bake) | **resource** (this module) |
| Create GPU objects / device-bound work | **graphics** (RHI) |

In producer-consumer terms: **resource is the producer/bridge.** The asset module hands it identity (`ShaderResource`: source path, stage, format, defines); it hands back CPU-side artifacts (`data::ShaderData`: stage, api, byte_code, source, entry); the RHI consumes them. Nothing here ever holds a GPU handle, and nothing here decides *when* something is loaded or freed.

**The name, disambiguated.** "resource" already means two other things in this engine — `asset::AssetPayload` (the payload variant) and `graphics::TextureResource` (a GPU image/view). This module is neither a store nor a GPU owner: it is the *processor*. The role split above (load vs. process vs. GPU) is what keeps the three uses apart; if it helps, read the namespace as "the resource processor."

## Key types

### `ResourcePipeline` — the facade — [`resource_pipeline.h`](../../engine/runtime/core/resource/resource_pipeline.h)

The module's public entry point. Owns a `ShaderProcessor` and a `ShaderCache`, initialized once with the chosen API:

```cpp
void Initialize(const ResourcePipelineContext& context);   // context.graphics_type
void ProcessShader(const std::vector<asset::ShaderPtr>& shaders);
```

### `ShaderProcessor` — the orchestrator — [`shader_processor.cpp`](../../engine/runtime/core/resource/shader_processor.cpp)

`Process(cache, assets, observer = nullptr)` drives the pipeline per shader: read source (`ReadText`) → `GenerateShaderHash(source+stage+entry+defines)` → `ShaderCache::Has` → on miss, `ShaderOperation::Run` (the compiler) → write `shader->data` (a `data::ShaderData`) → flip status to `Ready`. A failed compile flips status to `CompileFailed` and moves on — one broken shader doesn't kill the batch. The `observer` (default `nullptr`) is fire-and-forget progress reporting per phase: `(phase, done, total, shader)`. This is "process, don't load": it never registers assets or touches the asset graph.

### `ShaderCompiler` / `SPIRVCompiler` — the compilers — [`shader_compiler.h`](../../engine/runtime/core/resource/shader_compiler.h)

`ShaderCompileInput { source, file_name, stage, format, defines }` in, `vector<uint8_t>` out. `SPIRVCompiler` wraps shaderc: GLSL → SPIR-V, with per-compile macro definitions layered onto a shared option set so defines never leak between compiles ([`spirv_compiler.cpp`](../../engine/runtime/core/resource/spirv_compiler.cpp)).

### `ShaderOperation` — the sub-operation interface — [`shader_operation.h`](../../engine/runtime/core/resource/shader_operation.h)

The base interface for one stage of the processing pipeline: `GetPhase()` (which `ShaderProcessPhase`), `GetName()` (for logs/UI), and `Run(ShaderProcessContext&) -> bool` (success/failure). `ShaderCompiler` derives from it — its `Run` adapts the context into a `ShaderCompileInput` and forwards to `Compile`. Future preprocess/reflect/save stages implement it directly. `ShaderProcessContext` carries the data that flows between stages (source → byte_code, plus stage/format/defines), deliberately free of asset types so the interface stays decoupled from the asset graph.

### `ShaderCache` — content-addressed disk cache — [`shader_cache.h`](../../engine/runtime/core/resource/shader_cache.h)

Key = hash of (source + stage + entry + defines), file = `<hex hash>.spv` under `asset/shader/cache/<api>/`. Because the artifact is API-specific, the cache is keyed per API. A path-based key would serve stale artifacts after a source edit.

### `utility` — hashing + file I/O — [`utility.h`](../../engine/runtime/core/resource/utility.h)

`ReadText`/`ReadBinary`, `MurmurHash3_x64_128`, `GenerateShaderHash`. (Generic hashing could later live in core/base; today it lives here with the pipeline that uses it.)

## The data contract — CPU-side only

- **In:** `asset::ShaderResource` — identity (source path, stage, format, defines), `status: Uncompiled`.
- **Out:** `data::ShaderData { stage, api, byte_code, source, entry }` — the compiled artifact, `status: Ready`. `byte_code` = binary artifact (SPIR-V, Vulkan); `source` = text artifact (preprocessed GLSL, OpenGL).

`ShaderData` is defined in [`core/data/shader.h`](../../engine/runtime/core/data/shader.h) — the shared structs are `data/`'s job, the processing that fills them is `resource/`'s job. That is the split within core: **data/ = what is processed, resource/ = how it is processed.**

## Current state — built, callers on both ends

- `ResourcePipeline::ProcessShader` has **two callers**: the `CompileShader()` asset example ([asset_example.cpp](../../engine/example/asset/asset_example.cpp)) loads `simple_triangle.shader` and bakes both stages to SPIR-V through the pipeline (verified: fresh compile + cache hit), and the `rhi_example` demo (2026-08-16, TODO 2.3 of the [Vulkan decoupling](graphics/vulkanbackend.md)) bakes its shaders the same way at startup — replacing the build-time `glslc` step and giving the pipeline its first graphics-end caller. The **render module still has no caller** — the warmup pass and the RHI request path (the reconstruction's first wiring step) are still planned.
- Build: `Resource` is its **own static library** inside `core/` ([`resource/CMakeLists.txt`](../../engine/runtime/core/resource/CMakeLists.txt)); `Core` is just an interface aggregator that links it ([`core/CMakeLists.txt`](../../engine/runtime/core/CMakeLists.txt)). It links `Asset` PRIVATE.
- Known gaps: `ProcessShader` takes a flat stage vector when the natural compile unit is the whole program; it returns void so callers can't distinguish success from failure (per-shader `CompileFailed` status exists, but the caller must scan each shader's status to find it).

## Design notes

- **Stays in `core/`** (decision 2026-08-09). It is the one core-nested library that depends on a top-level module (`Asset`) — a mild layering smell — but because `Resource` is already its own static lib, hoisting it to `engine/runtime/resource/` later is a three-line move. Defer unless it outgrows shaders.
- **Warmup is the caller's job.** The render module, at init, reads a manifest of `.shader` paths and calls `asset.LoadSync` + `ProcessShader` for each, feeding the disk cache so later RHI requests are hits. The resource module only ever responds to `ProcessShader`; it never initiates.
- **Async callers ride a request queue.** The runtime half of the story — the render module's "async compile off the main thread" step — is the async resource queue ([async_resource_queue.md](../async/async_resource_queue.md)): a loading thread runs `ProcessShader` off-frame, the render thread drains finished artifacts under a frame budget. The queue exchanges requests, not payloads, so it stays type-agnostic as texture/mesh processing join. The module still only responds to `ProcessShader`; it never initiates.
- **Natural compile unit is the whole program**, not a stage — plan for `ProcessShaderProgram(const ShaderProgramResource&)`.
- **Artifacts are derived data** — they belong in the resource pipeline's cache, not in the asset graph.

## Planned operations — the full "process" story

`Process` drives one stage per API. For Vulkan: read → hash → cache-check → compile (shaderc) → write `ShaderData::byte_code`. For OpenGL: read → preprocess → write `ShaderData::source` — no cache, because the preprocess is cheap. `ShaderProcessor::keep_source_` (set at `Initialize` from the API) selects both the operation built and the `ShaderData` field written, so `Process` has no per-API branching. Compile and preprocess are in; the rest of the room is planned:

| Operation | What it produces | Why it's here |
|---|---|---|
| **Compile** (done — Vulkan) | `ShaderData::byte_code` (SPIR-V via shaderc) | the artifact itself |
| **Preprocessing** (done — GL) | `ShaderData::source`: the final assembled GLSL — includes resolved, defines injected | GL's artifact is source, not bytes; GL compiles it at runtime via `glShaderSource` |
| **Per-API artifact dispatch** (done) | SPIR-V for Vulkan, preprocessed GLSL for OpenGL, picked by `keep_source_` | one identity → N artifacts; no API checks scattered through the processor |
| **Diagnostics** (partial) | `CompileFailed` status (in) + compiler error text (not yet) | a broken shader must be visible to the caller, not silently consumed or recompiled every frame |
| **Reflection** | a reflection struct: vertex attribs, UBO/sampler/push-constant bindings, descriptor sets | turns hardcoded `PipelineDesc` construction — today `VulkanBackend::CreateGraphicsPipeline` hand-writes stage/layout/bindings — into data the render module reads |

**Ordering.** The `CompileFailed` status half of diagnostics is in; the compiler error text is next. Then reflection (the big payoff — it's what makes the render module's `PipelineDesc` data-driven instead of hardcoded). GL-side preprocessing for shared snippets / generated binding blocks extends the `PreprocessOperation` when they appear.

**Where reflection data lives.** A companion struct next to `data::ShaderData`, in `data/` — the bytes and the *description of the bytes* are different things, and the render module reads both.

## Refactor status

**Started — exercised by the asset example and the `rhi_example` demo (2026-08-16), no render-module caller yet.** `CompileShader()` in the asset example and the demo's startup bake both drive `ProcessShader` end-to-end (fresh compile + cache hit), but there are still no tests and the render module's warmup caller is still the reconstruction's job. The `ShaderOperation` seam and `CompileFailed` status are in; still planned while wiring: whole-program `ProcessShader`, a success signal, and the compiler error text.
