# Sakura Engine Render Graph Analysis

**Study snapshot:** `SakuraEngine/SakuraEngine`, branch `engine`, commit
[`c0fdb2bb30074058cb98df98b8134559d13d67b0`](https://github.com/SakuraEngine/SakuraEngine/tree/c0fdb2bb30074058cb98df98b8134559d13d67b0),
committed 2025-09-29. The repository was inspected from source on 2026-09-17.

**Question:** How does Sakura divide render-pipeline declaration, dependency
analysis, physical-resource management, synchronization, and command recording,
and which parts fit KimPeanutEngine's Vulkan/OpenGL renderer?

This is a design study, not an API or source-import proposal.

## Executive conclusion

Sakura's strongest idea is the separation between a small declarative frontend
and a sequence of explicit analysis/execution phases. A renderer creates typed
resource handles, declares render/compute/copy/present passes, and states each
resource use. The graph then extracts access information, derives dependencies,
schedules queues, analyzes lifetimes, plans barriers, resolves physical
resources and bind tables, and finally invokes pass callbacks.

The current implementation is also a useful warning against reading an
architecture document as shipped behavior. At the pinned revision, the graph is
rebuilt and all phases run every frame; culling is disabled; real memory aliasing
and multi-queue execution contain unimplemented paths; present execution is a
no-op; and callbacks receive CGPU encoders and issue raw CGPU commands. The
repository's phase document describes more capability than the executor
currently delivers.

KimPeanutEngine should therefore adopt Sakura's **phase boundaries, scoped
typed handles, imported/transient distinction, explicit access declarations,
and state handoff for imported resources**. It should not copy Sakura's raw-RHI
pass contexts, every-frame structural compilation, string-driven binding,
unversioned resource model, or aspirational aliasing/multi-queue surface.

## Actual structure

```text
renderer / sample
  create or import logical resources
  add render / compute / copy / present passes
  declare typed reads, writes, subresources, and requested CGPU states
                         |
                         v
RenderGraph frontend
  DependencyGraph containing PassNode <-> ResourceNode edges
  frame-scoped typed handles + named Blackboard
                         |
                         v
execute() phase chain, rebuilt each frame
  Cull [currently disabled]
  PassInfoAnalysis
  PassDependencyAnalysis + logical topological order
  QueueSchedule
  ExecutionReorderPhase
  ResourceLifetimeAnalysis
  CrossQueueSyncAnalysis
  MemoryAliasingPhase [Tier0 pooling in the active path]
  BarrierGenerationPhase
  ResourceAllocationPhase
  BindTablePhase
  PassExecutionPhase
                         |
                         v
CGPU device / command buffer / resource and view pools
```

`RenderGraphBackend` owns the CGPU device/queues it is given, three
frame-executor slots, texture/buffer/view pools, command buffers, fences, and
bind-table pools. This makes Sakura's graph both a logical planner and a
CGPU-backed executor. It is not a backend-neutral Render-only component in the
KimPeanutEngine sense.

## Frontend and data model

### Handles are typed and frame-scoped

The frontend has distinct buffer, texture, pass, and acceleration-structure
handles. The base storage contains a dependency-graph node ID and the graph's
frame index. Builders and `resolve` reject a handle from another frame. Texture
handles refine into SRV, RTV/DSV, UAV, and copy-subresource views carrying mip,
array, and aspect information; buffer handles can carry byte ranges.

This is a good safety property: logical handles cannot silently survive the
frame graph that created them. It is not resource versioning, however. A write
does not return a new version; all reads and writes continue to point at the
same `ResourceNode`.

### Access declaration creates graph edges immediately

Pass builders create edges directly:

- a texture/buffer read links `resource -> pass`;
- a render-target, UAV, or copy destination links `pass -> resource`;
- each edge stores the requested CGPU resource state;
- render attachments also carry load/store/clear information;
- named shader reads are later matched to reflected root-signature entries.

The builder distinguishes render, compute, copy, and present passes. The API is
pleasant for a caller because resource intent and attachment intent are close
to the pass declaration.

The dependency phase does not use version lineage. It scans passes in their
stored order, remembers the last access to each resource, and adds a dependency
from every later access to that last accessor. This is conservative and simple,
but it makes declaration order part of the dependency meaning, serializes
read-after-read access, and cannot diagnose an ambiguous or stale resource
version as precisely as an SSA-style model.

### Blackboard is convenience, not ownership

Names register passes and resources in a frame-local `Blackboard`. This lets
separate pipeline code find a resource without carrying the handle directly and
also supports debug names. It does not change resource ownership.

The cost is hidden coupling through strings. Sakura further uses the names on
read edges to find shader-reflection entries and automatically build bind
tables. That is convenient in its CGPU/reflection stack, but it couples graph
declarations to descriptor binding policy.

## Compilation and scheduling

Sakura's most reusable structural choice is that analysis results are explicit
objects consumed by later phases. Dependency analysis does not also allocate
textures; lifetime analysis does not record commands; allocation does not
decide scene policy. Each phase exposes queryable results, and GraphViz output
can include schedule, synchronization, barriers, aliasing, and lifetimes.

At this revision, however, `RenderGraphBackend::execute` constructs and runs the
whole phase chain every frame. After command submission, all pass/resource
nodes, edges, imports, and blackboard entries are deallocated or cleared. The
deferred sample consequently creates its G-buffer resources and declares its
passes again inside the frame loop.

That trade-off supports truly dynamic topology and keeps callback captures
frame-local. It also spends graph-analysis CPU every frame. Sakura's own
documentation advises avoiding per-frame rebuilds, but that advice does not
match the inspected executor and sample.

The current cull phase is enclosed by `if (false)`, and even its inactive body
only removes disconnected nodes; it is not export-root reachability culling.
The dependency phase computes a Kahn topological order and logical dependency
levels, but the inspected code does not produce a recoverable compile result
with diagnostics for cycles, missing producers, or invalid conditional flow.

## Resources, lifetimes, and state

### Imported resources

Importing a CGPU buffer or texture copies its physical description and records
an initial state. Imported nodes are borrowed and bypass graph allocation.
Persistent renderer resources can attach a `RenderGraphStateTracker`: their
last state seeds the next frame's import, and barrier analysis writes the
planned final state back for the next import. This is an important cross-frame
contract because a new graph cannot assume every persistent object starts in
`COMMON` or `UNDEFINED`.

### Transient resources and pools

Non-imported descriptions are resolved by `ResourceAllocationPhase` through
texture and buffer pools. Pool keys describe compatibility, pooled entries keep
their last state and frame mark, and view pools cache physical views. The graph
returns resources to those pools after submission and has fence/frame-index
helpers for garbage collection.

The phase chain always selects aliasing `Tier0`, which is descriptor-compatible
resource pooling rather than multiple logical resources sharing one physical
heap region. The builder's `enable_memory_aliasing` flag is stored but is not
consulted by the active execution path. Non-Tier0 alias execution reaches an
unimplemented branch. Claims of completed memory-aliasing percentages in the
repository documentation should therefore be treated as design targets, not
evidence from this revision.

### Barriers and subresources

Pass info retains texture mip/array ranges and buffer ranges. Barrier analysis
tracks texture state per mip/array element, tracks buffers as a whole, emits
UAV-to-UAV barriers, and can describe transitions, aliasing barriers, and
cross-queue synchronization. Imported resources start from their declared
state; transients start from their first use or pooled state.

This separation between **logical access**, **planned transition**, and
**backend encoding** is valuable. The concrete states are nevertheless CGPU
enums throughout the frontend, so Sakura's abstraction boundary is its common
RHI, not a Render-owned portable vocabulary.

## Command recording and execution reality

Pass setup is declarative, but pass execution is not RHI-free. A render callback
receives a `CGPURenderPassEncoderId`; compute receives a
`CGPUComputePassEncoderId`; copy receives a CGPU command buffer. The callback
calls functions such as `cgpu_render_encoder_draw` directly. The graph begins
the pass, resolves resources, creates and updates bind tables, binds a declared
pipeline, and then hands the encoder to the callback.

The active executor owns only a graphics command pool/buffer and submits only
the graphics queue. If more than one optimized queue timeline is present, the
execution path is explicitly unimplemented; cross-queue sync processing also
contains a TODO and currently relies on barriers. Multi-queue analysis exists,
but multi-queue execution is not complete at this commit.

Present is similarly split. A present node creates a terminal resource edge and
requests `PRESENT` state, while `execute_present_pass` is a no-op. `RenderApp`
performs the actual `cgpu_queue_present` externally. This makes the present node
useful as a dependency/state terminal even though presentation remains owned by
the host frame lifecycle.

## Deferred and ray-tracing samples

The deferred sample demonstrates the intended caller shape well:

1. acquire/import the backbuffer;
2. create logical G-buffer, depth, lighting, and composite textures;
3. declare G-buffer writes;
4. declare either fragment or compute lighting reads;
5. declare composite and final-blit dependencies;
6. execute the graph, then present externally.

The ray-tracing sample is more limited than its name suggests. It builds BLAS
and TLAS directly through CGPU in a separate command buffer and waits the queue
idle. Each frame it imports the existing TLAS as an acceleration-structure read,
runs a compute ray-query pass into a UAV texture, copies that texture to the
backbuffer, and presents. The graph already understands an imported AS handle
and bind-table entry, but it does not schedule the sample's AS build or scratch
lifetime.

That is still a useful incremental pattern for KimPeanut: the first ray-tracing
consumer can import a Graphics-owned TLAS and express its read/output hazards
before the graph attempts to own BLAS/TLAS build scheduling.

## What KimPeanutEngine should do

| Sakura pattern | Decision | KimPeanutEngine adaptation |
| --- | --- | --- |
| Renderer declares passes and logical resource uses | **Adopt** | `DeferredRenderer` remains policy owner and declares the graph. |
| Explicit analysis/execution phases | **Adopt, smaller first** | Start with validation/version dependencies, export-root culling, stable topological order, lifetimes, then execution. Add transition planning in R3.5. |
| Typed handles with frame-index stale-handle checks | **Adopt and strengthen** | Use graph identity/generation plus logical resource version; reject handles from another builder/compile epoch. |
| Typed subresource views | **Defer** | Begin whole-resource; retain a range field/extension point so mip/layer tracking can be added without replacing handles. |
| CGPU states attached directly to uses | **Modify** | Declare access, portable usage, and stage; let Vulkan/OpenGL translate that intent to native state or documented implicit behavior. |
| Imported resources with initial/final state handoff | **Adopt in portable form** | Import borrowed Graphics handles with logical usage; return final logical usage to the Graphics state owner. Never store Vulkan layouts in Render. |
| Physical resource and view pools | **Modify** | Graphics owns frame-safe pools and retirement. The Render graph supplies descriptions and lifetimes but never calls native allocation/free. |
| Rebuild and analyze the whole graph each frame | **Reject for the default path** | Cache an immutable structural plan; build a short-lived frame instance that supplies imports, conditions, packets, and callbacks. Recompile only a changed topology/description variant. |
| One unversioned resource node with last-access dependencies | **Reject** | A write creates a logical version. Dependencies come from producer/consumer versions, not merely previous declaration order. |
| Named Blackboard as the main composition mechanism | **Modify** | Keep stable debug names and optionally a typed frame-product registry; pass handles explicitly in core renderer code. Do not make string lookup authoritative. |
| Pipeline supplied in pass setup and bound by the graph | **Defer for R3** | Keep common `PipelineHandle` selection/binding inside existing pass callbacks. Add pipeline metadata only when graph validation or caching has a concrete need. |
| Shader-name-driven automatic bind tables | **Reject for R3** | Preserve `MaterialSystem`/`FrameContext` binding ownership. Graph uses describe hazards, not shader reflection or descriptor policy. |
| Callbacks receive raw RHI encoders/resources | **Reject** | `RenderGraphPassContext` exposes declared logical-to-common handles plus `CommandRecorder`; never native Vulkan/OpenGL/CGPU objects. |
| Present pass performs no commands but roots final state | **Adopt conceptually** | Model presentation and Editor composition as explicit terminal side effects/exports while `RenderSystem` keeps the frame bracket. |
| Lifetime analysis before pooling/aliasing | **Adopt** | Add non-aliasing transient reuse only after raster parity; aliasing remains a separately measured extension. |
| Multi-queue and true aliasing surface ahead of completed execution | **Reject as an initial scope** | Keep one graphics queue and no aliasing until a consumer, implementation, validation, and measurement exist. |
| Acceleration-structure resource kind | **Reserve, then add with the first RT consumer** | Start by importing a Graphics-owned TLAS read; schedule AS builds only in a later, explicit ray-tracing stage. |

## Resulting R3 design refinement

The high-level ownership proposed before this study remains valid:

```text
RenderSystem               owns frame/present/failure lifecycle
DeferredRenderer           owns scene and pass policy
RenderGraphBuilder         owns one logical declaration
CompiledRenderGraph        owns validated, cached structural decisions
RenderGraphFrame           owns per-frame imports, conditions, and packet refs
RenderGraphExecutor        drives common pass boundaries and CommandRecorder
Graphics/RHI               owns physical objects, native states, queues, fences
```

The study adds five concrete requirements to R3:

1. A handle contains graph identity/generation as well as resource/version, so
   stale or cross-graph use fails deterministically.
2. The cached compiled plan is separate from a per-frame instance; callbacks
   do not retain references captured during compilation.
3. Imported resources have an explicit portable initial/final usage contract.
4. Debug naming and optional typed product lookup are separate from shader
   binding and dependency authority.
5. R3 tests must prove culling, cycle/missing-producer diagnostics, deterministic
   ordering, and stale-handle rejection because the Sakura implementation does
   not provide those guarantees as a reusable tested contract.

## Primary source map

- [Frontend API and phase entry points](https://github.com/SakuraEngine/SakuraEngine/blob/c0fdb2bb30074058cb98df98b8134559d13d67b0/engine/modules/render/render_graph/include/SkrRenderGraph/frontend/render_graph.hpp)
- [Typed handles and pass contexts](https://github.com/SakuraEngine/SakuraEngine/blob/c0fdb2bb30074058cb98df98b8134559d13d67b0/engine/modules/render/render_graph/include/SkrRenderGraph/frontend/base_types.hpp)
- [Builder edge creation](https://github.com/SakuraEngine/SakuraEngine/blob/c0fdb2bb30074058cb98df98b8134559d13d67b0/engine/modules/render/render_graph/src/frontend/graph_builders.cpp)
- [Per-frame phase chain and cleanup](https://github.com/SakuraEngine/SakuraEngine/blob/c0fdb2bb30074058cb98df98b8134559d13d67b0/engine/modules/render/render_graph/src/backend/graph_backend.cpp)
- [Dependency derivation](https://github.com/SakuraEngine/SakuraEngine/blob/c0fdb2bb30074058cb98df98b8134559d13d67b0/engine/modules/render/render_graph/src/phases_v2/pass_dependency_analysis.cpp)
- [Barrier planning](https://github.com/SakuraEngine/SakuraEngine/blob/c0fdb2bb30074058cb98df98b8134559d13d67b0/engine/modules/render/render_graph/src/phases_v2/barrier_generation_phase.cpp)
- [Physical allocation](https://github.com/SakuraEngine/SakuraEngine/blob/c0fdb2bb30074058cb98df98b8134559d13d67b0/engine/modules/render/render_graph/src/phases_v2/resource_allocation_phase.cpp)
- [Pass execution](https://github.com/SakuraEngine/SakuraEngine/blob/c0fdb2bb30074058cb98df98b8134559d13d67b0/engine/modules/render/render_graph/src/phases_v2/pass_execution_phase.cpp)
- [Deferred sample](https://github.com/SakuraEngine/SakuraEngine/blob/c0fdb2bb30074058cb98df98b8134559d13d67b0/engine/samples/render_graph/rg-deferred/deferred.cpp)
- [Ray-query sample](https://github.com/SakuraEngine/SakuraEngine/blob/c0fdb2bb30074058cb98df98b8134559d13d67b0/engine/samples/render_graph/rg-raytracing/rg-raytracing.cpp)
