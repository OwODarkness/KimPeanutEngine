# Render Scene Documentation Guide

Read [repository instructions](../../../AGENTS.md), the [Render guide](../AGENTS.md),
this guide, [PLANS.md](PLANS.md), and [TODO.md](TODO.md) before changing this
submodule.

- `RenderScene` is a Render-owned recording boundary. It consumes common
  Render/Graphics contracts and borrows resolved resources.
- Graphics owns native command buffers and GPU lifetime; scene policy remains
  above the RHI.
- Keep architecture in `PLANS.md`, concrete S-stage design in `.plan/S*.md`,
  current work in `TODO.md`, and dated execution evidence in the relevant
  `.spec/journal/` entry.
