# Material System Documentation Guide

Read [repository instructions](../../../AGENTS.md), the [Render guide](../AGENTS.md),
this guide, [PLANS.md](PLANS.md), and [TODO.md](TODO.md) before changing this
submodule.

- Material System owns logical templates, instances, parameter validation,
  material pass policy, and frame-local binding descriptions.
- Asset owns serialized material identity and CPU data; Graphics owns GPU
  resources. Gameplay publishes asset identities, never Render material or RHI
  handles.
- Keep architecture in `PLANS.md`, concrete M-stage design in `.plan/M*.md`,
  current work in `TODO.md`, and dated execution evidence in
  `.spec/journal/render-material-system-v1.md`.
- Do not add deferred-PBR G-buffer or backend-native material details here
  unless the Material System contract itself changes.
