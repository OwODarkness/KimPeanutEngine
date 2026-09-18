# Render Graph Plans

**Status: built and closed with R3 (2026-09-18).** This page records
KimPeanutEngine's architecture after inspecting Sakura Engine's actual
render-graph source. The evidence and adopt/modify/reject decisions are in the
[Sakura analysis](sakura_analysis.md).

**Parent module:** [Render plans](../PLANS.md)

**Roadmap:** [Render Graph TODO](TODO.md)

**Concrete stage:** [R3](../.plan/R3.md)

## Objective

Replace the closed, manually executed fixed pass schedule with one declarative
graph that becomes the authority for pass dependencies, stable ordering,
conditional reachability, logical resource versions, and cross-pass lifetime
and transition planning. Preserve the current renderer, frame lifecycle,
material system, command-recording seam, and Vulkan/OpenGL behavior while
creating a clean dependency surface for later ray tracing.

The graph solves this specific problem:

> Render currently declares coarse pass reads and writes, but a separate fixed
> cursor executes the passes and backend-private target code performs state
> transitions without whole-frame dependency knowledge. Ray-tracing builds,
> storage outputs, and raster consumers would multiply those hidden ordering
> and synchronization relationships.

## Architectural boundary

```text
Runtime / Editor
      ↓
RenderSystem
  frame bracket, failure policy, external presentation hook
      ↓
DeferredRenderer
  scene policy, visibility, lights, materials, pipelines, frame packet
      ↓ declare
RenderGraphBuilder
  pass nodes, logical resource versions, access/usage/stage declarations
      ↓ compile
CompiledRenderGraph
  validated DAG, stable order, live passes, lifetimes, transition intents
      ↓ execute
RenderGraphExecutor
  physical resolution, barriers/pass boundaries, pass callbacks, profiling
      ↓
Graphics/RHI
  allocation, native translation, command streams, synchronization, retirement
```

The renderer is not replaced. `DeferredRenderer` stops manually invoking a
second authored pass order, but it remains the owner of deferred-rendering
policy and the implementation of each pass's recording work.

## Declaration, compilation, and execution

### Declaration

Declaration is CPU-only and makes no Graphics calls. A pass supplies:

- a stable name or typed identity;
- logical resource reads and writes;
- usage, pipeline stage, and attachment operations where relevant;
- an execution callback;
- optional condition, side-effect, external-owner, and profiling metadata.

The declaration API describes the render pipeline, not GPU pipeline objects.
`graphics::PipelineHandle` remains immutable executable state selected by the
renderer/material system and bound inside the execution callback.

### Compilation

Compilation validates and produces an immutable plan:

- reject invalid handles, cycles, reads without producers, incompatible uses,
  ambiguous writes, and illegal external/terminal dependencies;
- preserve declaration order as the deterministic tie-break between otherwise
  independent nodes;
- find passes reachable from exported outputs or explicit side effects;
- calculate logical-resource first/last use;
- produce portable transition intents without native Vulkan/OpenGL values;
- cache the plan until topology or resource descriptions change.

Compilation should normally occur outside an active backend frame. Per-frame
values and visibility change the execution packet, not the topology. A feature
whose I/O shape changes must select or rebuild a graph variant explicitly.

The immutable compiled plan is distinct from a short-lived frame instance. The
frame instance supplies current imported handles, enabled conditions, and
references to the immutable render packet. Pass callbacks therefore do not
capture frame-local references when the structural plan is compiled.

### Execution

The executor receives a valid compiled graph, the active `FrameContext`, the
common `CommandRecorder`, current imported resources, and the immutable render
frame packet. For every live pass it:

1. resolves logical resources to Graphics-owned physical handles or views;
2. applies the compiled transition intent through the common Graphics contract;
3. begins the declared attachment/presentation boundary when supported;
4. invokes the pass callback with a short-lived `RenderGraphPassContext`;
5. ends the boundary, records the outcome/profiling data, and releases
   transients after their last safe use.

The callback records `BindPipeline`, bindings, draw/dispatch, and later
acceleration-structure or ray commands. It never receives a Vulkan/OpenGL
command buffer or native resource.

## Resource model

### Logical handles and versions

Use distinct typed logical handles for textures and buffers, with an extension
point for acceleration structures:

```cpp
struct GraphTextureHandle { uint32_t resource; uint32_t version; };
struct GraphBufferHandle  { uint32_t resource; uint32_t version; };
```

The resource flow is an SSA-style chain and is authoritative for dependency
meaning: every write consumes the caller-selected prior version and returns a
new version for later passes. Fluent `pass.write()` helpers may improve caller
ergonomics, but they must preserve this explicit version lineage and must not
allow two writes to the same logical resource in one pass.

Exact representation is an implementation decision, but the semantic rules
are fixed:

- a handle identifies one logical resource version inside one graph;
- a handle also carries or is validated against a graph generation/compile
  epoch, so cross-graph and stale-handle use is rejected deterministically;
- a write produces a new version; readers name the version they consume;
- logical handles do not imply physical ownership or expose API-native state;
- physical handles are resolved only by the executing pass context and are not
  retained after their documented lifetime;
- graph handles do not enter `RenderWorld`, Gameplay, Asset, or persistent
  material records.

### Resource kinds

- **Imported:** an existing persistent resource owned by a renderer or Graphics
  manager. The graph references it and never destroys it. Its import declares a
  portable initial usage and execution reports its final usage to the Graphics
  state owner.
- **Transient:** declared by description; Graphics allocates/reuses the physical
  object, while the graph computes its logical lifetime.
- **Exported:** an output whose lifetime or side effect must survive graph
  culling, such as SceneColor, capture output, or presentation.

R3 first imports the current persistent targets. Transient creation lands only
after raster execution parity. Aliasing is a later measured optimization.

### Access vocabulary

`Read`/`Write` alone is not enough to infer synchronization. Each use needs at
least:

```text
logical handle + access + usage + pipeline stage
```

Initial usages cover color/depth attachment, sampled, storage, transfer source,
transfer destination, and presentation. R3 starts with whole-resource ranges
and one graphics queue. Subresources, queue ownership transfer, async compute,
and acceleration-structure-specific usages are gated extensions.

Do not require every immutable mesh or material texture to become a graph node
in the first stage. Track resources that carry hazards or lifetime relationships
between passes; expand coverage when a concrete producer/consumer requires it.

## Ownership and lifetime

| Concern | Owner |
| --- | --- |
| Scene, material, visibility, and pass policy | `DeferredRenderer` / Render |
| Frame bracket and application failure policy | `RenderSystem` |
| Logical graph and compiled dependency plan | Render Graph / Render |
| Persistent render targets and GPU objects | Existing Render owner through Graphics handles |
| Physical transient allocation and recycling | Graphics/RHI |
| Native layouts, barriers, queues, submission | Vulkan/OpenGL backend |
| Command contents of one pass | Render pass callback through common recorder |
| Safe retirement after submitted work | Graphics/RHI |

The graph may compute a logical lifetime interval, but it never frees a native
object directly. Imported resources retain their original owner; transient
resources retire through Graphics frame/fence safety.

## Relationship to current classes

- `RenderSystem` remains the Runtime-facing facade and frame-lifecycle owner.
- `RenderSceneCoordinator` continues publishing one immutable frame-scoped
  scene input.
- `DeferredRenderer` prepares the frame packet and declares the graph.
- Existing `Record*Pass` methods initially become execution callbacks; do not
  create a speculative virtual class hierarchy per pass.
- The compiled plan replaced the fixed sequence as the execution authority once
  per-frame parity against it was proven on both APIs; the fixed sequence and
  its executor no longer exist.
- `FrameContext` remains the owner of frame-local uniform ranges and binding
  sets; the graph does not absorb it in R3.
- Graphics remains an executor, allocator, and synchronization owner rather
  than the owner of scene/pass policy.

## Conditional and external work

Conditional capture and Editor composition must stay explicit:

- a pass needed only for diagnostic conversion may be disabled or absent in a
  compiled variant, but its producer/consumer rules remain statically valid;
- a SceneColor capture exports the existing target and does not invent a
  conversion write;
- Editor composition is an external terminal side effect that reads SceneColor;
- a required producer cannot depend solely on a condition that its consumer
  does not share;
- execution failure preserves the existing failed-frame rule: do not present
  partial required output as a successful frame.

## Ray-tracing extension path

R3 does not implement ray tracing. It must, however, leave a direct extension
path:

```text
geometry buffers → BLAS build/cache
BLAS + instances → TLAS build/update
GBuffer + TLAS   → ray-query lighting or ray-tracing output
RT output        → composite / tone map / capture
```

Render will declare which geometry/instances participate and when a build or
update is required. Graphics will own native acceleration structures, scratch
storage, build commands, synchronization, and retirement. OpenGL may omit these
passes through an explicit capability/fallback path; it must not emulate native
ray-tracing objects in the common contract.

## Performance policy

The graph is justified by correctness and dependency pressure, not an assumed
speedup. The current issue-9.7 evidence makes per-frame CPU cost a first-class
constraint:

- compiled topology is reused while unchanged;
- declaration/compilation/execution CPU times are reported separately;
- the raster migration must not materially regress the fixed Sponza profile;
- graph work does not claim to solve draw submission, descriptor, or packet
  costs that exist inside pass callbacks.

## Reference gate

The Sakura source study is complete at pinned commit
`c0fdb2bb30074058cb98df98b8134559d13d67b0`; see the
[source-backed analysis](sakura_analysis.md). It confirms the renderer/graph/RHI
ownership split while refining the handle generation, cached-plan/frame-instance,
and imported-state contracts.

Sakura's explicit phase pipeline, typed frame-scoped handles, resource pools,
and imported-state tracking are useful. KimPeanut deliberately does not copy
its every-frame compilation, unversioned last-access dependencies, string-based
automatic binding, raw CGPU pass contexts, or incomplete multi-queue/aliasing
surface. R3.1's formal plan review is complete and closed the pre-R3.2
decisions, so no further reference is required unless a future stage identifies
a concrete unresolved question.

## Non-goals for R3

- Replacing `DeferredRenderer` or `RenderSystem`.
- A renderer/plugin marketplace or arbitrary runtime pass registration.
- A new material, scene, ECS, or asset system.
- Vulkan/OpenGL objects in common Render contracts.
- Multi-queue scheduling, async compute, pass merging, or subpass synthesis.
- Memory aliasing before non-aliasing transient reuse is correct and measured.
- Ray-tracing pipelines, BLAS/TLAS implementation, or denoising.
- Solving issue-9.7 command-recording cost merely by changing scheduling.
