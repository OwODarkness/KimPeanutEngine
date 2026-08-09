# Render Module Design

Location: `engine/runtime/render/`

The render module is the engine's "what to draw and how" layer — the module that owns scenes, materials, cameras, render passes, and the requests for GPU pipelines. This is where the *call* originates: the render module asks the asset system for shader identity, asks the resource pipeline to bake it into artifacts, fills a `PipelineDesc`, and hands it to the RHI.

**Current state: the module is legacy and OpenGL-hardcoded.** It predates the RHI (`engine/runtime/graphics`) and calls raw GL directly everywhere. It is slated for reconstruction — the rest of this doc describes both what is there now and the target shape.

## Current state — the legacy GL renderer

The module compiles and runs, but it is a self-contained OpenGL renderer that ignores the asset system, the resource pipeline, and the RHI.

### `RenderShader` — raw GL shader objects — [`render_shader.h`](../../engine/runtime/render/render_shader.h)

Reads `.vert`/`.frag`/`.geom` GLSL **from disk at runtime**, calls `glCompileShader`/`glLinkProgram`, and exposes `glUniform*` setters (`SetFloat`, `SetVec3`, `SetMat`…). It is the old "shader = source path → GL program" model. It has no concept of stages as assets, no `ShaderData`, no compilation cache — every program is recompiled from source on first use.

### `ShaderPool` — hardcoded shader catalogue — [`shader_pool.h`](../../engine/runtime/render/shader_pool.h)

A `unordered_map<string, shared_ptr<RenderShader>>` keyed by a hardcoded list of categories:

```cpp
SHADER_CATEGORY_PHONG "phong_shader"         SHADER_CATEGORY_PBR "pbr_shader"
SHADER_CATEGORY_SKYBOX "skybox_shader"       SHADER_CATEGORY_NORMAL "normal_shader"
SHADER_CATEGORY_DIRECTIONALSHADOW …          SHADER_CATEGORY_POINTSHADOW …
SHADER_CATEGORY_OUTLINING …                  SHADER_CATEGORY_DEFER_PBR …
```

Every shader the game needs is a `#define` string. There is no data-driven `.shader` meta, no defines, no API selection.

### `RenderMaterial` — PBR/Phong factories — [`render_material.h`](../../engine/runtime/render/render_material.h)

`CreatePBRMaterial`/`CreatePhongMaterial`/`CreateMaterial(shader_name)` build a material from texture maps (`diffuse/albedo/normal/roughness/metallic/ao…`) and scalar/vec3 params. `Render()` binds textures by GL slot index (`texture_start_index`) and sets uniforms. Material identity is a shader name + a map list, not an asset program.

### `RenderScene` — the deferred renderer — [`render_scene.h`](../../engine/runtime/render/render_scene.h)

The biggest legacy piece: a hardcoded deferred pipeline with `g_buffer_`, a lighting pass (`ExecLightingRenderPass`), shadow passes (`ShadowManager`, directional/point/spot casters), skybox + environment map, post-process pipeline, a fullscreen triangle (`InitFullScreenTriangle`), and UBO/SSBO state (`ubo_camera_matrices_`, `light_ssbo_`). Raw `GLuint` handles for VAOs, VBOs, FBOs, UBOs, SSBOs are member fields. The render passes and their fixed-function GL state are baked into this one class.

### `RenderSystem` — the facade — [`render_system.cpp`](../../engine/runtime/render/render_system.cpp)

Owns `RenderScene`, `ShaderPool`, `TexturePool`, `RenderCamera`; per frame it runs `glBeginQuery(GL_PRIMITIVES_GENERATED)` / render / `glEndQuery` to count triangles. Called from `RuntimeContext::Tick`. This is the GL-only entry point the reconstruction replaces.

### Raw GL everywhere

GL state is threaded through the whole module: `FrameBuffer`, `RenderTexture`, `TexturePool`, `RenderMesh`, `render_pointcloud`, `skybox`, `gizmos`, `aabb_debugger` — all built directly on `GLuint` handles and `glad`. There is no `GraphicsContext`, no handle system, no backend abstraction.

### Build wiring

`Render` links `Core GameFramework Component Input glfw glad assimp stb_image` — **it does not link `Graphics`** ([`render/CMakeLists.txt`](../../engine/runtime/render/CMakeLists.txt)). `RuntimeLib` links it PRIVATE ([`runtime/CMakeLists.txt`](../../engine/runtime/CMakeLists.txt)). So today the legacy renderer and the RHI are two disconnected worlds: `Render` calls GL directly, `Graphics` is used only by examples and the editor. [`architecture_overview.md`](../architecture_overview.md) already flags `render: old version, need rebuild later`.

## Target architecture

The reconstruction makes the render module the **caller** in a one-way, high→low dependency chain:

```
render module
   ├──→ asset.AssetManager.LoadSync(.shader)     identity
   ├──→ resource.ResourcePipeline.ProcessShader  bake → ShaderData
   └──→ graphics.RenderBackend.CreatePipelineResource(PipelineDesc)   consume
                │
                └──→ nothing. RHI only reads byte_code + state.
```

### The ownership split

| Concern | Owner |
|---|---|
| Shader identity (`.shader` meta, stage descs) | `asset/` |
| Compile source → artifact (`ShaderData`, disk cache) | `resource/` → [resource_module.md](../resource/resource_module.md) |
| "I want this pipeline" — load, compile, fill desc, request | **render module** |
| Bake desc → GPU objects | `graphics/` RHI |

### The `PipelineDesc` seam

`graphics::PipelineDesc` ([`pipeline_types.h`](../graphics/graphics_module.md)) is the contract. The render module:

1. `asset.LoadSync(path)` → `ShaderProgramResource` (all stages, `Uncompiled`).
2. `resource.ProcessShader(stages)` → each stage's `ShaderData` is `Ready` (`byte_code`, `api`, `entry`).
3. Wrap each stage in a thin `graphics::Shader` implementation whose `GetCode()` returns `data->byte_code.data()` — the RHI interface **does not change** (`PipelineDesc` takes `graphics::Shader*`; the backend just stops receiving path-backed shaders).
4. Fill the rest of `PipelineDesc` (vertex layout, blend/raster/multisample state, descriptor bindings, attachment formats) from render-module-owned material/pipeline definitions.
5. `backend.CreatePipelineResource(desc)`.

### Warmup at init

A real game compiles its shaders at startup so first-frame pipeline requests are cache hits. The render module owns this: at init it reads a manifest of `.shader` paths, calls `asset.LoadSync` + `resource.ProcessShader` for each, populating `resource::ShaderCache` (disk) + `shader->data` (memory). Later backend requests hit the cache. Same `ProcessShader` API — just called eagerly at boot. (Later: material traversal instead of a manifest, and async compile off the main thread.)

## Reconstruction plan (ordered)

1. **Wire the resource pipeline in.** Give the render module (or `RuntimeContext`) an owned `ResourcePipeline`, initialized with the chosen `GraphicsAPIType`. It currently has no owner and no callers.
2. **Make the RHI a pure receiver.** In the backend, replace path-based `ShaderManager::CreateShader<API>(path)` with a `ShaderData`-backed `graphics::Shader` (wrap `ShaderData::byte_code`), and delete the commented-out asset-loading block in `VulkanPipelineManager` by doing it for real. Retire the `ShaderManager` path-keyed cache.
3. **Move `PipelineDesc` construction out of the backend** into render-module pipeline/material code. `VulkanBackend::CreateGraphicsPipeline` should only *bake* what it is handed.
4. **Add the render-module request path:** `asset.LoadSync(.shader)` → `resource.ProcessShader` → fill `PipelineDesc` → `backend.CreatePipelineResource`. Add a warmup pass in render init.
5. **Close the resource-pipeline gaps it will hit:** `ProcessShader` should take the whole `ShaderProgramResource` (all stages) as one unit, not a flat stage list; and add a `CompileFailed` status carrying the compiler error, so the render module can distinguish failure and not bake empty bytes.
6. **Retire the legacy path progressively.** `ShaderPool`/`RenderShader`/`RenderMaterial`/`RenderScene` raw-GL code gets replaced pass-by-pass with render-module code that consumes the RHI (meshes via `MeshManager`, textures via `TextureManager`, pipelines via `PipelineDesc`). Keep the module building at each step — the legacy and RHI-backed paths coexist until the last pass, then the GL-only code is deleted.
7. **Finish:** `Render` links `Graphics`; `RenderSystem` becomes the render-module facade that owns the backend, the warmup, and the scene graph.

## Invariants

- **One-way dependency, high → low:** render → asset, render → resource, render → graphics. `graphics` never reaches up (no `.shader` paths, no compilers, no asset IDs).
- **The render module is the only caller** of "load + compile + request pipeline." Neither the RHI nor the asset loader initiates a pipeline build.
- **Pipeline requests always carry baked bytes.** `PipelineDesc` shaders are backed by `ShaderData::byte_code`; the RHI never compiles or reads source.
- **Derived data lives in the resource pipeline.** The render module caches *pipeline* objects (keyed by `(program AssetID, api)`), never shader sources.

## Refactor status

**Legacy, OpenGL-hardcoded — reconstruction not started.** Target shape and ordering are captured above; see [the graphics module doc](../graphics/graphics_module.md) for the RHI side of the same change.
