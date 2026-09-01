# Render Capture Documentation Guide

Read [repository instructions](../../../AGENTS.md), the [Render guide](../AGENTS.md),
this guide, [PLANS.md](PLANS.md), and [TODO.md](TODO.md) before changing this
submodule.

- Render owns semantic view selection and conversion policy; Runtime owns
  output paths and PNG export; Graphics owns readback and synchronization.
- Capture APIs must expose owned CPU image data, not native images, framebuffers,
  command buffers, or backend handles.
- Keep architecture in `PLANS.md`, concrete C-stage design in `.plan/C*.md`,
  current work in `TODO.md`, and dated implementation evidence in the relevant
  `.spec/journal/` entry.
