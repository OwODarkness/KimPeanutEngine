# Gameplay Module Plans

**Status: active.** This page maps Gameplay architecture and its concrete stage
designs. Current work status belongs in [TODO.md](TODO.md); implementation
evidence belongs in `.spec/journal/`.

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
  define deterministic rollback/unload from a Runtime-owned instance.

The multi-stage ownership, migration, and acceptance contract is the
[Gameplay Level Asset GP7 spec](../../.spec/specs/gameplay-level-asset.md).
Later GP7.3–GP7.5 stage plans should be added only when their concrete design
work begins.

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
architecture, `.plan/<stage>.md` for a concrete future stage, `TODO.md` for
current status, and the matching journal for what actually happened.
