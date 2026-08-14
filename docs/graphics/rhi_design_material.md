# RHI Design Material — Synthesized from Zhihu Q&A

**Snapshot: 2026-08-14.** Distilled from the Zhihu question 《如何封装一个现代游戏引擎RHI层？》 (How to encapsulate a modern game engine RHI layer? — question 604591389). Raw capture was the pasted Zhihu content (since deleted from the repo root — this page is the surviving distillation). Five answers, three camps, 2023-07. This page is a **critical synthesis**, priority-ordered, with disagreements called out and mapped back to KimPeanutEngine. It is design *material*, not doctrine — several claims are hot takes (see [Sanity check](#sanity-check-read-this-too)).

## The thesis (what everyone converges on)

> RHI 是"按照图形引擎所需要的硬件特性，封装出来的底层硬件功能的一套 API" — a set of APIs exposing the **hardware features your engine needs**, not the entry points of any API documentation.

The recurring method: **穿透平台 API 定义，透视 GPU 硬件执行原理** — see through the API and the driver to the hardware execution model, then resolve each API's limits and give your own design. Platform APIs are "繁文缛节 + 私货" (red tape + vendor bias); your RHI is your own curated私货.

Claims nearly all answers share:
1. The goal is not "one API to rule all platforms" — it's "don't write `VkCreateImage` *and* `MTL::makeTexture` twice."
2. RHI is for professional graphics programmers (people who could write an RHI themselves), and it must **not** forbid `if(isMetal)` / `if(isVulkan)` branches — 不完全&限制.
3. RHI encapsulates hardware *features*; API extensions are just one expression of a feature.

## 1. The concept-by-concept abstraction map (the actionable core)

阳明先生's systematic rule:
- Same basic concept on all APIs → expose **one unified interface**.
- Concept exists on A but not B → expose the **detail** (don't fake it).
- Same function, many implementations / platform conflict → expose **only the detail your engine needs**.

Applied per concept:

| Concept | Unify (expose) | Keep low-level / native | Notes |
|---|---|---|---|
| **Device** | select a GPU by condition, pass as an opaque handle | fine-grained Features & Limits queries | don't 1:1 map caps; abstract common ones (max texture size), keep native query for the rest (Metal has no SubPass concept) |
| **Memory** (Texture/Buffer) | dimension, usage, storage class (CPU / CPU-GPU shared / GPU-only) | `VkDeviceMemory`↔`VkBuffer`/`VkImage` relation; `VkImage`+`VkImageView` = one `MTLTexture` | model on the most complex API (Vulkan), engineer around the others' gaps |
| **Shader** | translation tools (SPIRV-Cross; or macro + string) | the *real* problem is descriptor layout, not syntax | — |
| **Descriptor** | high-level abstraction bound to the RHI, exposed as **selectable strategies** | don't expose Bindless until an engine feature needs it | where the abstraction lives (RHI vs engine) is an *organizational* choice, not a code-hierarchy one |
| **PSO** | PSO = Shader + RenderPassDescriptor + RenderState, fully exposed | **serializable to a binary stream** | serialization *is* your pipeline cache |
| **RenderPass / Framebuffer** | model on Vulkan (most complex), fill gaps for others | SubPass: ignorable on desktop; Framebuffer = attachment container, avoid recreating | — |
| **Command** | expose Render / Compute / IO queues (may map to one real queue internally) | CommandBuffer/Pool: high-level abstraction + strategy options, like Descriptor | — |
| Fence / Semaphore / Event / Barrier / Swapchain | same treatment as above | | — |

## 2. Command management — the deepest technical lesson (SaeruHikari)

**The trap:** top-down RHI designs often copy Metal's software interface — user creates multiple Command Encoders / Lists per scope — to "support Metal." That breaks D3D12/Vulkan backends:

- Scattered encoders can't map to a single per-thread allocator.
- Scattered lists can't map to one D3D12List / VkCmdBuffer.
- Short/零散 lists cost performance: the spec wants one allocator per thread and long batched command lists — a **memory-layout** constraint, not a GPU-speed one.

**Root cause:** each hardware command processor (CP) uses different data structures and memory management; the API layer's job is converting your commands into what that hardware accepts. Real hardware models:
- a big **ring buffer** mixing all micro-engine commands (console-style),
- **separate instruction pages** per micro-engine (Xbox/D3D12-style),
- a **dual-sided buffer** recording commands + PSO state together (newest; indirect state setting).

**The clean resolution — lock the recording context at Begin/End Dispatch.** The RHI returns an encoder at BeginPass and reclaims it at EndPass (CGPU: `cgpu_cmd_begin_render_pass` / `cgpu_cmd_end_render_pass`). This keeps a single allocator and a single command buffer while honoring Metal's encoder semantics, with no extra encoder objects to manage. Scope rules fall out naturally:
- **Render dispatch** (inside a render pass): rasterization commands — scissor, vertex buffers, graphics PSO.
- **Compute dispatch**: free, outside a render pass, registers satisfied (binders set, barriers correct).
- **Transfer/copy**: most flexible — any engine, except the gfx engine inside an open render pass.

## 3. The command architecture alternative: stateless, reorderable commands (LuisaRender)

MaxwellGeng's position: "unify across APIs" and "keep each API's features" are **inherently in conflict at one layer** — so don't build one RHI, build layers:

- Backend resources are integer **handles**; commands are **context-free / stateless** (`draw(resource_a, resource_b)`), so the backend can reorder them. This moves the RenderGraph/FrameGraph work to *after* the command queue instead of before, **decoupling command recording from execution**.
- Shader portability via **AST codegen**: DSL → AST → target code, instead of language translation. The AST is verifiable, serializable, and multi-frontend (C++ DSL, Python, even HLSL→AST→HLSL now that Clang parses HLSL). Translation's failure mode — reverse-engineering source then forward-emitting, so you can't tell which step broke — disappears.
- Custom/extended commands just carry raw pointers (`ID3D12Device*`, `MTL::ComputePipelineState*`) plus a resource read/write declaration, hosted as plugins (they ship Direct-Storage, Optix-Denoiser, AMD-FSR2, Intel-XeSS, Direct-ML this way).

Cost: this is a large system. It's the radical end of the spectrum — a direction to know about, not to adopt casually.

## 4. The debate — RHI vs "xx on xx" layers (decision-relevant)

**Skeptic (往昔):** RHI is a legacy product from when no universal cross-platform path existed. Porting layers (Vulkan Layer, VulkanOn12, MoltenVK) are easier to land and increasingly common: pick **one API as your primary dev path**, follow the newest features first, skip the double-encapsulation call overhead. RHI's fatal pain: one universal interface over deeply heterogeneous devices forces a fragile glue layer (stability weaker than a driver); new features always lag, which is commercially fatal; a device-interface divergence can force an RHI rewrite. The Layer mechanism itself is a great low-level design — it lets you interpose between driver and API and implement features yourself.

**Rebuttal (SaeruHikari):** porting layers are the deepest software trap, not an escape. MoltenVK/dxvk translate commands at fine granularity → large host cost at submit. Vulkan's sparse-texture definitions are so complex a compat layer can never cover them (MoltenVK issue #1700 — a fundamental feature, unsupported and unlikely ever to be). Layers are unstable across machine / layer version / driver version. If you know the hardware feature set, writing a parallel RHI beside the platform APIs isn't that hard — you don't need a translation layer.

**Synthesis (阳明先生):** the Layer + "access per-device features without per-backend interfaces" combination is **still an RHI**. But the skeptic's framing carries a real risk: it couples render-pipeline logic with hardware-feature code — RenderGraph logic interspersed with raw API calls — and that maintenance burden grows. The RHI's real job is letting the bottom-layer developer amortize their own maintenance cost; it's not there to force-feed ease on engine programmers.

## 5. Match the RHI shape to the goal

A RHI should know which of these it is (阳明先生):
- **Engine performance** → low-level, minimal per-call loss.
- **Emulator / legacy games** → stability > performance.
- **Bridging a render SDK into an engine** (bgfx) → ease-of-use > stability > performance.

Italink gives the minimal-RHI list: wrap **Resources** (Pipeline, Texture, Buffer, Sampler, CommandBuffer, Swapchain, RenderTarget, ShaderResourceBindings), wrap **Commands** (create / draw / pipeline / buffer read-write-blit / sync / query), **translate Shaders**, and normalize API differences (NDC, uniform-buffer alignment). QRhi and bgfx are at this level. For a *large* engine that's not enough — advanced features have concept structures that mismatch across APIs, and wrapping an RHI is low-ROI dirty work. His advice for that case: **"just use Vulkan or DX12 directly"** and pick features freely.

## 6. What it means for KimPeanutEngine (our take)

Mapped to the RHI work in [graphics_module.md](graphics_module.md) and [TODO.md](TODO.md):

1. **The thesis validates our direction.** We already abstract common concepts cross-API (`PipelineDesc`, [pipeline_types.h](../../engine/runtime/graphics/backend/common/pipeline_types.h)) while managers take engine data, not paths ([graphics_module.md](graphics_module.md)). That's the "abstract common, keep native / take data not paths" rule.
2. **The PSO formula endorses the pipeline-cache TODO.** PSO = Shader + RenderPassDescriptor + RenderState, fully exposed and **serializable to binary**. That is exactly "PipelineDesc is a *key*, render module dedupes by hash" from [TODO.md](TODO.md) — and adds a requirement: keep PSOs serializable, which the content-addressed `resource::ShaderCache` is compatible with.
3. **Descriptor as selectable strategy.** Our `PipelineDesc::descriptor_binding_descs` is a strategy option; we don't need Bindless — keep it out of the RHI surface until a renderer actually needs it.
4. **The command trap (§2) is a warning for the future.** We're at "one command buffer, `BeginFrame`/`EndFrame`/`Present`" ([render_backend.h](../../engine/runtime/graphics/backend/common/render_backend.h)). When command recording is added, design the begin/end-dispatch encoder model from the hardware execution model — don't bolt Metal's multi-encoder interface on.
5. **The debate argues for Vulkan as primary.** "Use one API" (Italink) and "one primary dev path" (skeptic) both support treating Vulkan as the reference backend and GL as the port — consistent with the existing leak note that `VulkanBackend::CreateGraphicsPipeline` builds the desc internally today.
6. **Stateless commands leave room for a future render graph.** If command recording stays context-free, a later render graph can reorder *after* the queue (LuisaRender) rather than being wired into recording. Not needed now — just worth not painting into a corner.
7. **Shader translation is the school we're already in.** We use shaderc (GLSL→SPIR-V) + `keep_source_` for GL ([resource_module.md](../resource/resource_module.md)). SPIRV-Cross and AST-codegen are the noted alternatives if we hit the translation-debugging wall.

## Sanity check (read this too)

- The porting-layer performance critique (§4) is SaeruHikari's strong opinion, not consensus. MoltenVK and dxvk are widely deployed and performant in practice; the sound part is the *general* point that fine-grained command translation inflates submit host cost — it doesn't generalize to "layers are always worse."
- The "RHI is legacy / don't build one" position is a minority hot take, and the thread's own synthesis is that the Layer approach, combined with per-feature access, *is* an RHI. The real debate is thickness and placement, not whether an abstraction exists.
- Hardware-model claims (§2; "descriptor heap is software adaptation for AMD", "multi-queue is a software concept for NVIDIA") are correct in substance but simplified — treat as intuition, not architecture specs.

## Sources

- Question: 《如何封装一个现代游戏引擎RHI层？》, https://www.zhihu.com/question/604591389 — answers by SaeruHikari (CGPU/Sakura author), MaxwellGeng (LuisaRender), 往昔, 阳明先生, Italink (2023-07).
- The thread's own references: [The-Forge](https://github.com/ConfettiFX/The-Forge), [WebGPU](https://github.com/gpuweb/gpuweb), [CGPU](https://github.com/SakuraEngine/SakuraEngine), [LuisaRender](https://luisa-render.com/), [Vulkan Layer](https://www.vulkan.org/porting), [VulkanOn12](https://microsoft.github.io/DirectX-Specs/d3d/VulkanOn12.html), [MoltenVK](https://github.com/KhronosGroup/MoltenVK) (issue [#1700](https://github.com/KhronosGroup/MoltenVK/issues/1700)), [SPIRV-Cross](https://github.com/KhronosGroup/SPIRV-Cross), [Vulkan Memory Allocator](https://gpuopen.com/vulkan-memory-allocator/), [bgfx](https://github.com/bkaradzic/bgfx), [O3DE RHI](https://www.o3de.org/docs/atom-guide/dev-guide/rhi/), [QRhi](https://doc.qt.io/qt-6/qrhi.html)
