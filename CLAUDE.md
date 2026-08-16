# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

KimPeanut Engine — a C++17 game engine. Layered architecture: Editor → Engine → Resource System → RHI → Vulkan/OpenGL. The README (written in Chinese) is the broad reference; core principles there: data-driven resources (Asset → Processor → GPU data), decoupled shader/texture/mesh via the resource pipeline, cross-platform via RHI, and a disk shader cache.

## Project status

The current state of the world lives in [docs/status.md](docs/status.md) (done / in progress / next) and [docs/dead_code.md](docs/dead_code.md) (not-in-build, stale, and slated-for-retirement code — don't "fix" it as if live). Update these as work lands. Slash commands: `/status`, `/build`, `/test`, `/run`.

## Build

MSVC, Visual Studio 17 2022 generator, C++17 (`/utf-8`). Build tree is `build/`:

```bash
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug
```

Main executable is `KimPeanutEngine` (engine/editor/main.cpp). Third-party DLLs are copied to each target's output dir via `copy_thirdparty_dlls` / `copy_runtime_dependencies`.

## Tests

GoogleTest, registered with `gtest_discover_tests` under `engine/test/unit`:

```bash
cmake --build build --config Debug
ctest --test-dir build -C Debug                    # all suites
ctest --test-dir build -C Debug -R AudioUnitTest   # one suite by target name
```

## Code style

Source-code comments stay short — the *why*, not the *what*, max 3 lines; long rationale goes in `docs/`. Full rules: `/concise-comments` skill ([.claude/skills/concise-comments/SKILL.md](.claude/skills/concise-comments/SKILL.md)).

## Asset module (`engine/runtime/asset`)

The full design reference lives in [docs/asset/asset_module.md](docs/asset/asset_module.md). The facts that matter for working in this code:

- `AssetManager` is a singleton. Public API: `LoadSync(path) -> AssetID`, `LoadAsync(path) -> std::future<AssetID>` (same pipeline on a worker thread), `RegisterAsset(info)`, `GetAsset(id) -> Asset*` (**non-owning**), `UnRegisterAsset(id)`, and `GetResource<T>(id) -> shared_ptr<T>`.
- **Two-tier model.** `Asset` is a metadata wrapper (id, name, path, dependency graph, payload). The payload `AssetPayload` is a `variant` of `shared_ptr<T>` (Model/Mesh/Texture/Audio/Shader/ShaderProgram). The cache owns the wrapper; the payload is ref-counted and may outlive its wrapper (borrowed by renderers via `GetResource`).
- **`AssetID` = (slot, generation, AssetType)**, packed into a `uint64` for logging. Slots are recycled, so the generation is the stale-id guard: `GetAsset` validates it and a stale id returns `nullptr`, never a different asset.
- **One `AssetCache` per `AssetType`**, holding `vector<unique_ptr<Asset>>` slots (owner of record), a `HandleSystem` (generation counters, in `engine/runtime/core/base/handle.h`), and `path_index` — `unordered_map<string, AssetID>` for dedup.
- **`Key(path)`** normalizes separators + case. Invariant: the `path_index` lookup/insert/erase keys must all be `Key()` of the same string. Only `LoadSync` populates the index; loader sub-resources register directly and bypass it.
- **Load flow:** `LoadSync` → extension sniff (`utility.h`) → `AssetType` → dedup via `path_index` → `LoadByExtension` dispatches to a per-type loader → loader fills `AssetRegisterInfo` → `RegisterAsset` allocates a slot. Assimp model loading registers sub-meshes (`KPAT_Mesh`) via `RegisterAsset` directly.
- **Dependency graph:** each asset has `dependencies` (what it uses) and `ref_assets` (reverse — who uses it), maintained by `AddReferences`/`RemoveReferences`. `CanDelete` is the unload gate: refuse while referenced.
- **Thread-safe.** `state_mutex_` (`std::recursive_mutex`, recursive because the public methods compose internally) guards `caches_`/`path_index` in every public method; `load_mutex_` serializes `LoadByExtension` because the loader instances are shared and not thread-safe. Lock order is always load → state. Loads are serialized, so async loading wins on *not blocking the caller*, not on parallelism.

### Refactor status

Complete. The migration to the ownership model above (shared_ptr → unique_ptr wrappers, `weak_ptr` path_map → `path_index`) is done and the manager is thread-safe per the mutex model.

## Graphics (RHI) and render modules

Design references: [docs/graphics/graphics_module.md](docs/graphics/graphics_module.md) (the RHI) and [docs/render/render_module.md](docs/render/render_module.md) (the render module — **legacy, OpenGL-hardcoded, slated for reconstruction**). The facts that matter for working in this code:

- **The render module is the caller.** It asks `asset` for shader identity, asks `resource` to bake it, fills a `graphics::PipelineDesc`, and hands it to the RHI. The RHI **responds, it never initiates**: it knows nothing about `.shader` files, compilers, or asset IDs.
- **`PipelineDesc`** ([engine/runtime/graphics/backend/common/pipeline_types.h](engine/runtime/graphics/backend/common/pipeline_types.h)) is the cross-API contract. Its shaders are `data::ShaderData*` directly (resource-pipeline output) — no `graphics::Shader` wrapper; each backend reads the field its own API needs (Vulkan `byte_code`, OpenGL `source`).
- **Known RHI leaks (partially fixed 2026-08-15):** shaders now arrive as `data::ShaderData` (`PipelineDesc` holds `data::ShaderData*`; the `graphics::Shader`/`ResourceShader`/`ShaderLoader` seams retired); the path-keyed `ShaderManager` (`shader_manager.*`, `shader_factory.h`, `vulkan_shader.*`, `opengl_shader.*`) is **retired and deleted**. `VulkanBackend::CreateGraphicsPipeline` and `OpenglBackend::CreatePipeline` now **take** the `PipelineDesc` and bake it. Remaining: the build-time `glslc` → `.spv` step (feeds the `rhi_example`, which still reads `.spv`/`.vert` files as the caller — the RHI itself doesn't), and the stale `ShaderModule` seam (takes `data::ShaderData` but not in the build).
- **Frame recording is public (Phase 4, 2026-08-15):** the Vulkan backend's `BeginFrame` prepares the frame's scene command buffer (`GetCurrentSceneCommandBuffer()`); a caller records draws, then `EndFrame` ends rendering, submits and presents. The demo that lived in the backend is now `render::RenderScene` — the render module's first real scene, Vulkan-specific (raw `vkCmd*` against the scene command buffer). The OpenGL backend keeps its own baked scene.
- **The resource pipeline is orphaned:** `ResourcePipeline::ProcessShader` has no callers yet. The render-module request path and a startup warmup pass (manifest → `LoadSync` + `ProcessShader`) are the reconstruction's first wiring step.
- **Build wiring:** `RuntimeLib` links `Graphics` PUBLIC, `Render` PRIVATE; `Render` links `Graphics` PRIVATE (landed 2026-08-15, TODO 5.1) — the demo scene `RenderScene` records through the RHI's frame API while `RenderSystem` itself stays API-agnostic and un-reconstructed.

## Resource module (`engine/runtime/core/resource`)

Design reference: [docs/resource/resource_module.md](docs/resource/resource_module.md). The resource module is the **CPU-side processing layer** — the middle of Asset → Process → GPU. It **processes** (compiles/bakes) CPU-side data into artifacts; it does **not** load/unload (that's asset) and does **not** touch the GPU (that's the RHI). The facts that matter:

- `ResourcePipeline` (facade) → `ShaderProcessor` → `ShaderCompiler`/`SPIRVCompiler` (shaderc) → `ShaderCache` (content-addressed, per-API). In: `asset::ShaderResource` (identity). Out: `data::ShaderData { stage, api, byte_code, source, entry }` — `byte_code` is the binary artifact (SPIR-V, Vulkan), `source` the text artifact (preprocessed GLSL, OpenGL); `api` picks which is set.
- **Currently orphaned:** `ResourcePipeline::ProcessShader` has no callers. The render-module reconstruction is what gives it a caller + warmup.
- `Resource` is already its own static library inside core (links `Asset` PRIVATE), so hoisting it out of core later is a three-line move — deferred unless it outgrows shaders.
- Known gaps to close while wiring: `ProcessShader` returns void and takes a flat stage list (compile unit should be the whole program). `CompileFailed` status and the `ShaderOperation` seam are already in.

## Script module (`engine/runtime/script`)

Design reference: [docs/script/script_module.md](docs/script/script_module.md). Two layers, hosted inside the runtime engine (never the editor):

- **`ScriptLua` / `LuaVM`** (`engine/runtime/script/lua/`) — the generic Lua hosting layer. Owns one sol2 state; **engine-agnostic** (no asset paths, no `kpengine` classes; only `lua`/`sol2`/`Log`). Non-throwing API: `bool` + `KP_LOG`/`LastError()` for failures, `std::optional` for lookups. `std::optional<sol::protected_function_result> CallFunction(...)` — `nullopt` = missing function, engaged + `!valid()` = Lua error.
- **`Script`** (`engine/runtime/script/script_core.cpp`) — the engine-binding seam, currently empty. Engine bindings, `package.path` wiring to `GetScriptDirectory()` (`asset/script/`), asset-pipeline script loading, hot-reload all land here.
- `RuntimeLib` links `Script` PUBLIC. The script layer must never depend on the editor.
- **Sandbox:** opens only `base/string/table/math/package`; `package.loadlib` + `package.cpath` stripped; per-execution instruction budget (`lua_sethook`, default 10M) aborts runaway scripts instead of hanging the game thread; `SOL_ALL_SAFETIES_ON` on `ScriptLua`.
- **Gotcha:** `sol::safe_script`/`safe_script_file` **throw** with the default on-error handler — always pass `sol::script_pass_on_error` when the caller handles the result (as `LuaVM` does).
- Headless unit tests: `engine/test/unit/script/` (`ScriptUnitTest`, no DLL copy — static-only deps).

## Editor module (`engine/editor`)

Design reference: [docs/editor/editor_module.md](docs/editor/editor_module.md). The editor is the **core center** — a thin application shell hosting the runtime and owning the ImGui tool UI. Facts that matter:

- **Layout (2026-08-13 restructure)** — headers sit beside their sources, grouped by concern: `editor.h` (the shell), `context/` (`EditorContext` hub), `ui/` (`EditorUI` manager) + `ui/component/` (the widget tree), `platform/` (WSI/renderer backends), `log/`. All editor includes use the engine root (`"editor/..."`).
- **The shell** — `Engine` owns `editor::Editor` (`RuntimeLib` PRIVATE-links `EditorLib` and vice-versa — a known circularity). `Initialize` (main) → `InitEditorUI`/`Tick`/`CloseUI` (render thread) → `Clear` (main, after join). All ImGui work runs on the render thread where the GL/Vulkan context exists.
- **No GLFW/API hardcoding** — `EditorUI` owns `IEditorImguiWSI` (GLFW impl today) + `IEditorImguiRenderer` (GL + Vulkan impls), chosen by `GraphicsAPIType`. Components only ever see ImGui. The Vulkan renderer isn't usable yet (`GraphicsContext::native` is passed null).
- **Component UI** — a composable tree of `EditorUIComponent` (one virtual `Render()`); `EditorWindowComponent` is the panel base. Data binding is raw pointers re-read each frame (ImGui immediate-mode idiom).
- **Dead seeds** — `EditorSceneManager`/`EditorActorControlPanel` and the scene/camera component impls were all-comment and got **deleted** in the restructure; recover from git history when rebuilding scene picking.
