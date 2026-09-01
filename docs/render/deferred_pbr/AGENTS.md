# Deferred PBR Documentation Guide

Read [repository instructions](../../../AGENTS.md), the [Render guide](../AGENTS.md),
this guide, [PLANS.md](PLANS.md), and [TODO.md](TODO.md) before changing this
submodule.

- Keep Asset → Resource → Render → Graphics ownership intact; Render must not
  load environment or material files directly.
- Keep common contracts API-neutral and the renderer on its explicit ordered
  pass schedule until measured evidence justifies a graph.
- Keep architecture and policy in `PLANS.md`, concrete D-stage design in
  `.plan/D*.md`, roadmap status in `TODO.md`, and dated execution evidence in
  `.spec/specs/render-deferred-pbr.md` / `.spec/journal/render-deferred-pbr.md`.
- Do not duplicate journal landing narratives in `TODO.md` or `PLANS.md`.
