# Async Resource Queue Design (the request-based producer/consumer seam)

Location: generic transport in `engine/runtime/core/async/` (header-only lib `Async`); the asset-flavored request in `engine/runtime/asset/asset_load_request.h`

The async resource queue is the engine's **off-frame loading + GPU-upload seam**. It moves the expensive CPU work (shader compile, texture decode, mesh processing) and the GPU bakes (pipeline creation) **off the render frame**, so a synchronous `LoadShaders`-style call never blocks a frame. It is a **request-based** queue: the two ends exchange small `AssetLoadRequest` objects, not payloads.

This is the planned caller shape for the resource pipeline (the render module's "async compile off the main thread" step in [render_module.md](../render/render_module.md)) and the runtime half of the warmup story.

## The problem it solves

The engine already has a game thread and a render thread ([engine.cpp](../../engine/runtime/engine.cpp) splits them; [runtime_global_context.h](../../engine/runtime/runtime_global_context.h) tracks both ids). If the render thread ever runs a synchronous load+compile, it stalls the frame. Two costs must be kept off the frame:

1. **CPU processing** — `resource::ProcessShader` (GLSL → SPIR-V via shaderc) is tens of ms of pure CPU; texture decode and Assimp meshes are the same shape. None of it needs a current GPU context.
2. **GPU bake** — `vkCreateGraphicsPipeline` is *also* slow (the driver compiles it). Budgeting the queue drain protects against this too, which a payload-passing design would miss.

## The decision

| Question | Answer |
|---|---|
| The ends | **Render module** (consumer) ↔ **async loading thread** running asset load + resource process (producer). The RHI is *not* an end — the render module drains the queue and calls the RHI, keeping "the RHI responds, it never initiates." |
| Who owns the queue | **`RuntimeContext`** — the composition root both sides can reach without a dependency cycle (it already owns `graphics_api_type_`). `RenderSystem` gets a pointer for the per-frame drain; the loading thread gets one for processing. |
| What it stores | **`AssetLoadRequest` objects** — small, type-agnostic, homogeneous. One struct covers shader *and* texture *and* mesh; adding an asset type never changes the queue contract. |
| The payload | Attached to the request **only once `Ready`** — a `shared_ptr` that pins the artifact so it can't be unloaded between `Ready` and the drain. The render thread bakes from it directly. |

## Key types

### `AssetLoadRequest` — the unit of work — [`asset_load_request.h`](../../engine/runtime/asset/asset_load_request.h)

Lives in the **Asset** module, because it is asset vocabulary ("load this") and the render module — the caller — already depends on asset. Keeping it there means `core/async` holds nothing asset-flavored.

```cpp
enum class RequestState { Queued, Processing, Ready, Baked, Failed };

struct AssetLoadRequest {
    RequestID             request_id;   // caller's key into the render-side ready cache
    AssetType             type;         // KPAT_ShaderProgram | KPAT_Texture | KPAT_Mesh | ...
    std::string           path;         // load key
    AssetID               asset_id{};   // resolved by the worker once loaded
    RequestState          state;        // guarded by the queue mutex while in flight
    std::shared_ptr<void> payload;      // pinned artifact once Ready; cast by `type`
    // render-side bake options, set at enqueue:
    //   KPAT_ShaderProgram → vertex layout / blend / raster state (the non-shader half of PipelineDesc)
    //   KPAT_Texture       → sampler settings
};
```

`payload` is type-erased (`shared_ptr<void>`) and reinterpreted through `type` by the consumer. For a shader program it is the stage handles whose `data` are filled by `ProcessShader` (a `data::ShaderData` read), for a texture a `data::TextureData`, for a mesh a `data::MeshData`.

### `AsyncQueue<T>` — the generic transport — [`async_queue.h`](../../engine/runtime/core/async/async_queue.h)

A small mutex-guarded FIFO in `core/async` (namespace `async`), deliberately generic — it carries whatever the caller puts in it, so core never learns about asset types. Two instances compose the two pipes:

- **incoming queue** — render → worker. New `Queued` requests. The worker pops these, does the CPU work, pushes the request back as `Ready`.
- **ready queue** — worker → render. Finished artifacts. The render thread drains these per frame.

Two SPSC pipes keep the flow direction explicit and each is lock-free-able later; a `std::mutex` + `std::deque` per pipe is enough now (each is touched at most once per frame per side). It is MPSC-safe too, so multiple producers can come later without a type change.

### The loading thread — the producer

A single dedicated worker (the engine already has a game/render split; add a loader) that:

1. Pops a `Queued` request from the incoming queue.
2. `asset.LoadSync(path)` → resolves `asset_id`; for a shader program, `resource.ProcessShader(stages)` (the worker's `type` switch is where per-type processors plug in — `ShaderProcessor` today, texture/mesh processors later).
3. Sets `state = Ready`, attaches the `payload` shared_ptr, pushes onto the ready queue.

**One worker makes `ProcessShader` naturally serialized** — it sidesteps the resource module's thread-safety gap (nothing in the processor is documented thread-safe) without adding a mutex.

### The drain — the consumer, once per frame

In `RenderSystem::Tick` (or a dedicated render-side drainer), before draw:

```cpp
void DrainFrameBudget(AsyncAssetQueue& ready, size_t max_items, TimePoint max_time) {
    while (auto* req = ready.TryPop()) {
        if (--budget < 0 || now() > max_time) { ready.PushFront(req); break; }  // leave rest for next frame
        Bake(req);                     // fill PipelineDesc / upload texture, via the RHI
        ready_cache_.insert({req->request_id, BakedHandle});   // moved from pending to ready
    }
}
```

`Bake` is the render module filling the cross-API description (`graphics::PipelineDesc` for a shader program, sampler settings for a texture) and handing it to the RHI — the RHI is still pure receiver; it only *bakes what it is handed*.

### `ReadyCache` — render-side, keyed by request

`unordered_map<uint64_t request_id, BakedResult>` where `BakedResult` is whatever the render module baked (a `PipelineHandle` + the `ShaderData`-backed `graphics::Shader` wrappers for a program, a `TextureHandle` for a texture). Callers poll:

```cpp
bool IsReady(uint64_t request_id);          // present in ready cache?
PipelineHandle GetPipeline(uint64_t request_id);   // null until ready
```

**This polling is what kills the stutter.** The frame never waits on any specific request — it draws a placeholder / skips the draw until the handle lands.

## Data flow

```
render thread                     loading thread (asset + resource)
─────────────                     ────────────────────────────────
needs shader X
  └─ push {Queued, KPAT_ShaderProgram} ─▶ incoming ─▶ pop
                                                     asset.LoadSync(path)
                                                     resource.ProcessShader(stages)
                                                       └─ payload = stage handles, state = Ready
                                                       └─ push ──▶ ready queue
RenderSystem::Tick
  └─ DrainFrameBudget(K, max_ms)
       ├─ Bake → PipelineDesc → RHI.CreatePipelineResource
       └─ ready_cache_[request_id] = handle
  └─ materials poll IsReady(request_id); placeholder until true
```

## Why request, not payload

1. **The queue contract is stable.** It carries one small type forever; new asset types plug in downstream (a `Processing` branch + a bake case), not in the queue.
2. **The payload fetch belongs on the render thread.** Baking GPU objects requires a current context, so a worker shipping finished payloads just to have the render thread consume them buys nothing. The request lets the render thread control *when* it fetches — at drain time, where baking must happen anyway.
3. **The state machine decouples the two threads' paces.** The expensive CPU work happens while a request is `Processing`; the render thread only ever pops `Ready` items, so it never waits on a compile.

## Ownership & layering

- **`core/async` stays pure.** It holds only the generic `AsyncQueue<T>` — no asset, `data`, or graphics types. `AssetLoadRequest` lives in the Asset module (`asset_load_request.h`): both the render module (the caller, already linked to asset) and the loading thread reach it, so core never depends on a top-level module. This avoids reproducing the `core/resource`-style smell.
- The queue *instance* is owned by `RuntimeContext`. `RenderSystem` holds a pointer (set at init) for the drain; the loading thread holds one (passed at spawn) for processing. No dependency cycle: both ends only know `async::AsyncQueue<T>` and the `asset::AssetLoadRequest` they push through it.
- `asset` and `resource` are unchanged: they still respond to calls (`LoadSync`, `ProcessShader`), never initiate. The queue is purely the transport.

## Lifetime, dedup, failure

- **Pinning:** `payload` is a `shared_ptr`, so the queue holds the artifact alive even if the asset wrapper is unloaded before the drain reaches it — the request survives its asset, and the render thread bakes from a live payload.
- **Dedup:** before enqueueing, the render module checks the ready cache *and* a small pending-set keyed by `(type, path)`; the same texture is never queued twice.
- **Failure:** a failed compile/decode sets `state = Failed` (reusing the resource module's `CompileFailed` status). The drain skips failed requests and reports the id; it does not block or retry.

## Phasing

**Build the generic skeleton now, wire the shader path first** — it's the only real processor today:

1. `core/async`: `AssetLoadRequest`, `AsyncAssetQueue` (two SPSC pipes), the loading-thread loop. ~one header + one cpp.
2. Render side: `RuntimeContext` owns the queue; `RenderSystem` drains per frame; the shader-program `Bake` case fills `PipelineDesc` and calls `CreatePipelineResource` (reusing the render-module reconstruction's `PipelineDesc` seam).
3. `IsReady`/`GetPipeline` polling in the material path with a placeholder while pending.
4. Later, per `type`: texture/mesh processing branches on the worker + bake cases in render. No queue surgery.

The sync warmup pass (render-module reconstruction step 4) is still worth doing — it populates the disk cache at boot so first runtime requests are fast hits; the async queue handles everything that comes *after* boot.

## Open questions (deferred, not blockers)

- **Priorities** in the incoming queue (urgent spawn-time shader vs. background streaming textures) — add a priority field if measurement shows contention.
- **Cancellation** — dropping a request the render module no longer wants. For v1, let it bake and age out of the cache.
- **Cross-frame ordering** — a program whose bake is split across two frame budgets must not present a half-built pipeline. The ready-cache insert is atomic per request, so this is already safe; the bake loop just must not expose partial state.

## Relation to the module docs

- [resource_module.md](../resource/resource_module.md) — the queue is where `ProcessShader` gets its runtime caller, and the home of the "async compile off the main thread" step. `resource` still only responds; it never initiates.
- [render_module.md](../render/render_module.md) — the render module is the initiator and the drainer; the queue is the async half of its warmup plan.
- [graphics_module.md](../graphics/graphics_module.md) — the RHI is the last hop in the drain, unchanged: it takes `PipelineDesc` + `data::*` and bakes. The queue never hands the RHI an asset id or a path.
