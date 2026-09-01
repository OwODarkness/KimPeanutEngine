# Render Module Documentation Guide

This directory documents the Render runtime module. Read the repository
[agent contract](../../AGENTS.md), this guide, and the relevant submodule
guide before changing Render documentation or implementation.

## Boundaries

- Render owns scene policy, `RenderWorld`, materials, cameras, pass scheduling,
  logical render targets, and render-ready descriptions.
- Asset owns file identity and CPU-side asset lifetime; Resource owns derived
  CPU artifacts; Graphics/RHI owns GPU allocation, synchronization, and native
  API translation.
- Render documentation must not make Gameplay own Render handles or make
  common Render/Graphics contracts expose Vulkan/OpenGL implementation types.

## Documentation layout

- `PLANS.md` is the Render architecture and submodule map.
- `TODO.md` is the Render-level cross-cutting roadmap and submodule index.
- Each submodule has its own `AGENTS.md`, `PLANS.md`, `TODO.md`, and `.plan/`
  stage files when the work is substantial.
- `PLANS.md` describes architecture and policy. Concrete stage design belongs
  in `.plan/<stage>.md`; it must not be expanded into a second roadmap.
- `.spec/specs/` and `.spec/journal/` remain the centralized execution spec
  and factual implementation journal. Do not duplicate journal entries here.

## Change workflow

1. Read the module `PLANS.md`, `TODO.md`, and the affected submodule guide.
2. Preserve links between architecture, stage design, roadmap, spec, and
   journal instead of copying the same content across layers.
3. Keep TODO entries concise: goal, acceptance condition, status, and links.
4. Put dated implementation notes, commands, results, investigations, and
   remaining risks in the matching `.spec/journal/` file.
