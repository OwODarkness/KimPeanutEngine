# Render Graph Documentation Guide

Read [repository instructions](../../../AGENTS.md), the
[Render guide](../AGENTS.md), this guide, [PLANS.md](PLANS.md),
[TODO.md](TODO.md), the [R3 stage design](../.plan/R3.md), and the
[Sakura analysis](sakura_analysis.md) before changing this submodule.

- Render owns pass policy, graph declarations, logical dependencies, and the
  compiled execution plan.
- Graphics/RHI owns physical GPU resources, native barriers, command encoding,
  submission, synchronization, and safe destruction.
- A graph resource handle is logical and frame/graph scoped. It must not escape
  into Gameplay, Asset, persistent material state, or backend-native code.
- Graph declaration and compilation perform no GPU work. Commands are recorded
  only during execution through the common `CommandRecorder` seam.
- Keep architecture in `PLANS.md`, current work in `TODO.md`, the concrete R3
  migration in `../.plan/R3.md`, formal stage reviews in `.review/`, and dated
  execution facts in `.spec/journal/`.
- The Sakura reference study and scoped R3.1 design review are complete for
  R3.2. Reference code is evidence rather than a template to copy; R3.3+
  remain separately gated.
