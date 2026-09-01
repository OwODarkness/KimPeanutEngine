# Material System TODO

**Status: M1–M6 complete.** Architecture: [PLANS.md](PLANS.md). Concrete
stage designs are in [`.plan/`](.plan/). This roadmap tracks logical material
ownership and resolution; deferred shading belongs to the
[Deferred PBR submodule](../deferred_pbr/TODO.md).

## Stages

- [x] [M1 — handles and immutable templates](.plan/M1.md)
- [x] [M2 — instances and parameter validation](.plan/M2.md)
- [x] [M3 — pipeline and resource resolution](.plan/M3.md)
- [x] [M4 — frame-local material bindings](.plan/M4.md)
- [x] [M5 — MeshProxy and draw-list integration](.plan/M5.md)
- [x] [M6 — Material Asset V1](.plan/M6.md)
- [ ] [M7 — pass-specific material expansion](.plan/M7.md)

## Explicitly deferred

- Material graph/node editor and shader generation.
- Bindless resources, virtual textures, parameter arrays, and push-constant
  policy.
- PBR G-buffer encoding, deferred lighting, shadows, and post-processing; see
  the Deferred PBR roadmap.
- Material domains beyond surface (decal, UI, terrain, water, hair).

## Evidence

- [Material System journal](../../../.spec/journal/render-material-system-v1.md)
- [Gameplay/bootstrap material journal](../../../.spec/journal/gameplay-bootstrap-material-asset.md)
