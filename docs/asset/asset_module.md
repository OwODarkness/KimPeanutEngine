# Asset Module Design

Location: `engine/runtime/asset/`

The asset module is the engine's "get me this file as a typed resource" layer. It dedups loads by path, assigns stable IDs, tracks which assets depend on which, and hands out resource payloads to the rest of the engine (renderers, audio, editor).

## Key types

### `AssetType` and `AssetID` — [`common.h`](../../engine/runtime/asset/common.h)

```cpp
enum class AssetType : uint16_t {
    Undefined, KPAT_Model, KPAT_Texture, KPAT_Audio,
    KPAT_Shader, KPAT_ShaderMeta, KPAT_Mesh,
};
```

`AssetID` = `{ uint32_t id; uint16_t generation; AssetType type; }`. It is the engine's stable reference to a loaded asset and packs to a `uint64_t` (`Pack()`/`Unpack()`) for logging and serialization:

```
[16-bit type][16-bit generation][32-bit slot id]
```

**The generation is not decoration.** Slots are recycled, so `(slot, generation)` is what distinguishes "the mesh that used to be at slot 3" from "the texture that is now at slot 3." A stale `AssetID` must resolve to `nullptr`, never to a different asset.

### `Asset` vs `AssetResource` — [`asset.h`](../../engine/runtime/asset/asset.h)

The module is **two-tier**:

- **`Asset`** — the metadata wrapper: `AssetID`, `name`, `abs_path`, the payload, and the dependency graph (`ref_assets` = who uses me, `dependencies` = what I use). Owned by the cache.
- **`AssetResource`** — the payload, a `std::variant` of `shared_ptr<ModelResource | MeshResource | TextureResource | AudioResource | ShaderResource | ShaderMetaResource>`. Ref-counted, borrowed by subsystems, and **may outlive its wrapper**.

`AssetRegisterInfo` is the struct loaders fill in: the payload, path, name, type, and declared dependencies.

### `AssetCache` — [`asset_manager.h`](../../engine/runtime/asset/asset_manager.h)

One cache per `AssetType`, three fields with three distinct jobs:

| Field | Type | Job |
|---|---|---|
| `assets` | `vector<unique_ptr<Asset>>` | slot storage — **owner of record** |
| `handles` | `HandleSystem<AssetHandle>` | generation counters for slot validity |
| `path_index` | `unordered_map<string, AssetID>` | pure index, normalized path → id (dedup) |

`HandleSystem` lives in [`engine/runtime/core/base/handle.h`](../../engine/runtime/core/base/handle.h): a free-slot list plus a per-slot generation vector. `Create()` reuses a free slot and bumps its generation; `IsHandleValid(h)` compares the generation.

## Ownership model — the contract

1. **The cache owns the `Asset` wrapper** (`unique_ptr`). `GetAsset` returns a non-owning `Asset*`; callers must not hold it past `UnRegisterAsset`.
2. **The payload is shared** (`shared_ptr<T>` inside the variant). `GetResource<T>(id)` returns the payload's own `shared_ptr`, so a borrowed resource outlives its wrapper.
3. **`path_index` never owns anything** — no `weak_ptr`, no `shared_ptr`. It is a pure string → `AssetID` lookup.
4. **`GetAsset` must validate the generation** (slot recycled → old id → `nullptr`) **and null-check the slot** (freed-but-not-yet-reused slots still pass the generation check, because `HandleSystem::Destroy` doesn't bump the generation until reuse).

## Load / unload flow

### `LoadSync(path)` / `LoadAsync(path)` — the load entry points

`LoadSync` runs this pipeline on the calling thread; `LoadAsync(path) -> std::future<AssetID>` runs the *same* pipeline on a worker thread (`std::async`), so they behave identically except for which thread blocks:

1. Sniff the extension (`GetFileExtension` in `utility.h`), map to an `AssetType`; bail on unknown.
2. Dedup: `key = Key(path)`, look up `path_index`. If the entry exists **and** still resolves via `GetAsset` (i.e. the id isn't stale), return the cached id.
3. Otherwise dispatch to a per-type loader via `LoadByExtension`, which fills an `AssetRegisterInfo`.
4. `RegisterAsset(info)` allocates a slot, assigns the id, and records dependencies.
5. Index the loaded asset: `path_index[Key(asset->GetPath())] = id`.

Because the loaders are shared instances, concurrent loads serialize on `load_mutex_`; the dedup check runs again under the state lock after loading, so two concurrent requests for the same file can't double-register. Note: destroying a `LoadAsync` future without `get()`/`wait()` blocks until the load finishes (`std::async` semantics).

### `RegisterAsset(info)`

Allocates a slot from `handles.Create()` (reusing a free slot if any, else appending), stores the wrapper at that slot, wires up the `AssetID`, and calls `AddReferences` so every listed dependency gains this asset in its `ref_assets`.

Loaders also call `RegisterAsset` directly for **sub-resources** — e.g. the assimp loader registers each `KPAT_Mesh` (see `LoadMesh`). Sub-resources bypass `path_index`, which is why a mesh and its parent model can share the same file path without colliding: they live in different per-type caches and only the top-level `LoadSync` call indexes anything.

### `UnRegisterAsset(id)`

The only eviction path. Must validate the id's generation, refuse while `CanDelete` is false (still referenced), remove this asset from its dependencies' `ref_assets`, erase its `path_index` entry, reset the slot, and destroy the handle.

### Reference tracking

`dependencies` is declared by the loader (`ref_assets` on the target), `ref_assets` is reverse-maintained by `AddReferences`/`RemoveReferences`. This is a *semantic* graph, distinct from `shared_ptr` refcounting — it answers "can I unload X while Y still uses it?"

## Threading model

The manager is thread-safe via two mutexes:

- **`state_mutex_`** (`std::recursive_mutex`) — guards `caches_` and `path_index`. Every public method (`LoadSync`, `RegisterAsset`, `GetAsset`, `UnRegisterAsset`, `AddReferences`, `RemoveReferences`) takes it. It is **recursive** because the public methods compose internally (`RegisterAsset` → `AddReferences`, `UnRegisterAsset` → `RemoveReferences` → `GetAsset`, `LoadSync` → `GetAsset`/`RegisterAsset`); a plain mutex would self-deadlock.
- **`load_mutex_`** (`std::mutex`) — serializes `LoadByExtension`, because the loaders are single shared instances (one assimp, one stb, one miniaudio) and are not thread-safe.

Lock ordering is strictly **load → state** (never state → load), so there is no deadlock. The split exists so a `GetAsset` on the game thread only contends during the short dedup/register critical sections, not during another thread's disk I/O.

Consequence: loads are **serialized**, not parallelized — async loading wins on *not blocking the caller*, not on throughput. Parallel loading requires per-thread loader instances or a loader pool.

## Loaders and dispatch

`LoadByExtension` dispatches by `AssetType`:

- `KPAT_Model` → `Assimp_ModelLoader` (also emits `KPAT_Mesh` sub-resources)
- `KPAT_Texture` → `Stb_ImageLoader`
- `KPAT_Audio` → `MiniAudio_AudioLoader`
- `KPAT_ShaderMeta` → `ShaderMetaLoader` (emits `KPAT_Shader` sub-resources)

Each loader is an interface (`model_loader.h`, `image_loader.h`, `audio_loader.h`, `shader_meta_loader.h`); the concrete implementations are swappable. The manager owns them as `unique_ptr` and currently hard-codes the concrete types in its constructor.

## Invariants

- `path_index` lookup key == insert key == erase key, all via `Key()` of the same path string. If a loader rewrites `info.path`, dedup degrades to "reload every time" — never a wrong hit.
- `Key(path)` folds case and `\` → `/`. Case-folding is correct on Windows but would be wrong on a case-sensitive filesystem.
- A freed slot's `path_index` entry is erased before the slot is reused, so the index never points at a recycled id (belt: erase; suspenders: the generation check in `GetAsset`).
- Only `LoadSync` populates `path_index`.

## Refactor status

Complete. The migration from "shared_ptr everywhere + `weak_ptr` path map" to the ownership model above is done — `vector<unique_ptr<Asset>>` slots, `path_index` as `map<string, AssetID>`, generation-checked `GetAsset`, `CanDelete` as a hard gate, and the threading model above.

## Known smells / next steps

- `#define DEBUG` in `asset_manager.h` leaks a macro into every TU that includes it.
- `ref_assets`/`dependencies` are `vector<AssetID>` with linear `find`/erase — O(n²) on large scenes; a `set` on the packed `uint64` would scale.
- Loads are serialized on `load_mutex_` because the loaders are shared instances; per-thread loader instances or a loader pool would unlock parallel loading. `GetAsset` locks on every call — uncontended that's cheap, but an unlocked variant is the escape hatch if it ever becomes a hot path (do **not** revert to returning `shared_ptr`).
- Unloading today is manual in the backends (e.g. `vulkan_backend.cpp` unregisters a model *and* its sub-mesh by hand). Consider cascading eviction: when `UnRegisterAsset` empties a dependency's `ref_assets`, evict it too.
