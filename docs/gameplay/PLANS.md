# Gameplay Module Plans

**Status: active.** This page maps Gameplay architecture, concrete stage
designs, and task-scoped review records. Current work status belongs in
[TODO.md](TODO.md); implementation evidence belongs in `.spec/journal/`.

## Module architecture

Gameplay owns authoritative mutable Actors and components. It publishes copied,
value-only mesh, light, and camera source descriptions across narrow Render-owned
sink interfaces. Runtime owns system ordering and destroys Gameplay before
Render so active components can retire their source tokens safely.

```text
Asset CPU identity/data       Runtime lifecycle
          |                         |
          v                         v
     authored inputs ------> GameplayWorld
                                  |
                           Actors/components
                                  |
                         copied source values
                                  v
                         Render-owned registries
```

The detailed landed Actor, component, controller, and source-boundary design is
in [Gameplay Module Design](gameplay_module.md).

## Current stage plans

- [GP7.1 — level asset schema and dependency graph](.plan/GP7.1.md) — define
  the complete closed V1 CPU format and Asset-owned dependency transaction.
- [GP7.2 — static-mesh level instantiation and rollback](.plan/GP7.2.md) —
  preflight level dependencies, transactionally create Gameplay Actors, and
  define deterministic rollback/unload from a Runtime-owned instance. Its
  completed review is recorded in [`.review/GP7.2.md`](.review/GP7.2.md).
- [GP7.3 — level lights, cameras, and environment source](.plan/GP7.3.md) —
  extend the level transaction to all V1 Actor kinds and add one value-only,
  frame-boundary-resolved environment source. Its current review findings are
  recorded in [`.review/GP7.3.md`](.review/GP7.3.md).
- [GP7.4 — startup-level migration and validation fixtures](.plan/GP7.4.md) —
  close authored material dependencies, reduce bootstrap to one startup-level
  identity, transactionally enter the frame loop, remove legacy Render
  bootstrap plumbing, and author the PBR/point/spot fixtures. Its current review
  findings are recorded in [`.review/GP7.4.md`](.review/GP7.4.md).
- [GP7.5 — runtime evidence and GP7 handoff](.plan/GP7.5.md) — independently
  audit startup/rollback, source and dependency lifetime, rebuilt dual-backend
  execution, visual captures, and final GP7 documentation closure.
- [Post-GP7 — selectable startup-level override](.plan/STARTUP_LEVEL_OVERRIDE.md)
  — add a typed `--startup-level` launch override so fixtures can be selected
  without editing the durable Bootstrap default; live switching is excluded.
  Landed 2026-09-02.
- [GP8 — light transform alignment](.plan/GP8.md) — make point, spot, and
  directional lights consume SceneComponent transforms before Reflection RF2.

The multi-stage ownership, migration, and acceptance contract is the
[Gameplay Level Asset GP7 spec](../../.spec/specs/gameplay-level-asset.md).
GP7.5 was the final GP7 stage. The startup-level override is a separate
post-GP7 toolability seam and does not change the closure baseline.

## Ownership direction

```text
Runtime -> Gameplay -> Render source contracts
Runtime -> Asset
Render  -> Asset / Resource -> Graphics RHI
```

Gameplay may retain AssetIDs used by live components and opaque Render source
tokens used to request later update/destruction. It does not own Asset loading
policy, Render's resolved state, or GPU resources.

## Navigation rule

Read this file for the module map, `gameplay_module.md` for detailed landed
architecture, `.plan/<stage>.md` for a concrete stage design,
`.review/<task-id>.md` for its formal review record, `TODO.md` for current
status, and the matching journal for what actually happened.
