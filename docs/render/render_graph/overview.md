# Render Graph Overview

KimPeanutEngine's render graph is the Render-owned description and future
execution layer for cross-pass GPU work. It will replace the manually executed
`FixedRenderPassSequence` only after the existing raster frame has been
characterized, the design has passed an external-reference gate, and parity is
proven on Vulkan and OpenGL.

**Status: R3.2 pure compiler complete; runtime migration gated.** R3.0 baseline
evidence, the pinned Sakura study, and the scoped R3.1 review are recorded.
The graph model/compiler and R3.3 fixed-schedule compatibility proof are
implemented without changing runtime execution; the next step is R3.4
graph-directed execution.

```text
RenderSystem frame lifecycle
        ↓
DeferredRenderer scene policy + immutable frame packet
        ↓ declares
RenderGraph passes + logical resource handles
        ↓ compiles
ordered/cullable execution plan + lifetime/transition plan
        ↓ executes
common CommandRecorder + Graphics-owned physical resources
        ↓
Vulkan / OpenGL
```

The renderer remains. It decides what to draw, prepares visibility and material
data, chooses pipelines, and declares pass/resource relationships. The graph
owns dependency ordering and cross-pass resource reasoning. Pass execution
still records API-neutral commands; the graph does not become a second RHI.

## Document map

- [Architecture](PLANS.md) — ownership, data flow, resource model, lifecycle,
  and non-goals.
- [Roadmap](TODO.md) — R3 stages, gates, and acceptance-oriented work.
- [R3 stage design](../.plan/R3.md) — concrete migration and validation plan.
- [Sakura analysis](sakura_analysis.md) — actual source structure, current
  implementation limits, and adopt/modify/reject decisions.
- [Parent Render plans](../PLANS.md) and [Render TODO](../TODO.md) — module-wide
  context.
- [Agent guide](AGENTS.md) — local documentation and implementation rules.

The [R3.2 spec](../../../.spec/specs/render-graph.md) records the authorized
CPU-only boundary. Record further implementation and validation facts in dated
`.spec/journal/` entries; do not turn these design pages into an execution log.
