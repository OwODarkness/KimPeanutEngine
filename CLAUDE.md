# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project overview

KimPeanut Engine — a C++17 game engine. Layered architecture: Editor → Engine → Resource System → RHI → Vulkan/OpenGL. The README (written in Chinese) is the broad reference; core principles there: data-driven resources (Asset → Processor → GPU data), decoupled shader/texture/mesh via the resource pipeline, cross-platform via RHI, and a disk shader cache.

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

## Asset module (`engine/runtime/asset`)

The full design reference lives in [docs/asset/asset_module.md](docs/asset/asset_module.md). The facts that matter for working in this code:

- `AssetManager` is a singleton. Public API: `LoadSync(path) -> AssetID`, `LoadAsync(path) -> std::future<AssetID>` (same pipeline on a worker thread), `RegisterAsset(info)`, `GetAsset(id) -> Asset*` (**non-owning**), `UnRegisterAsset(id)`, and `GetResource<T>(id) -> shared_ptr<T>`.
- **Two-tier model.** `Asset` is a metadata wrapper (id, name, path, dependency graph, payload). The payload `AssetResource` is a `variant` of `shared_ptr<T>` (Model/Mesh/Texture/Audio/Shader/ShaderMeta). The cache owns the wrapper; the payload is ref-counted and may outlive its wrapper (borrowed by renderers via `GetResource`).
- **`AssetID` = (slot, generation, AssetType)**, packed into a `uint64` for logging. Slots are recycled, so the generation is the stale-id guard: `GetAsset` validates it and a stale id returns `nullptr`, never a different asset.
- **One `AssetCache` per `AssetType`**, holding `vector<unique_ptr<Asset>>` slots (owner of record), a `HandleSystem` (generation counters, in `engine/runtime/core/base/handle.h`), and `path_index` — `unordered_map<string, AssetID>` for dedup.
- **`Key(path)`** normalizes separators + case. Invariant: the `path_index` lookup/insert/erase keys must all be `Key()` of the same string. Only `LoadSync` populates the index; loader sub-resources register directly and bypass it.
- **Load flow:** `LoadSync` → extension sniff (`utility.h`) → `AssetType` → dedup via `path_index` → `LoadByExtension` dispatches to a per-type loader → loader fills `AssetRegisterInfo` → `RegisterAsset` allocates a slot. Assimp model loading registers sub-meshes (`KPAT_Mesh`) via `RegisterAsset` directly.
- **Dependency graph:** each asset has `dependencies` (what it uses) and `ref_assets` (reverse — who uses it), maintained by `AddReferences`/`RemoveReferences`. `CanDelete` is the unload gate: refuse while referenced.
- **Thread-safe.** `state_mutex_` (`std::recursive_mutex`, recursive because the public methods compose internally) guards `caches_`/`path_index` in every public method; `load_mutex_` serializes `LoadByExtension` because the loader instances are shared and not thread-safe. Lock order is always load → state. Loads are serialized, so async loading wins on *not blocking the caller*, not on parallelism.

### Refactor status

Complete. The migration to the ownership model above (shared_ptr → unique_ptr wrappers, `weak_ptr` path_map → `path_index`) is done and the manager is thread-safe per the mutex model.
