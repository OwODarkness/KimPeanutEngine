# Gameplay Module Documentation Guide

Read the repository [agent contract](../../AGENTS.md), [module plans](PLANS.md),
and [roadmap](TODO.md) before changing Gameplay design or implementation.

## Boundaries

- Gameplay owns mutable world state, Actor/component lifetime, transforms,
  gameplay ticking, controller state, and authored runtime composition.
- Asset owns file identity, immutable CPU asset payloads, dependency edges, and
  CPU-side asset lifetime.
- Runtime owns startup/shutdown ordering and future live level instances.
- Render receives copied source values and owns proxy, camera/light selection,
  material resolution, render policy, and GPU-facing descriptions.
- Gameplay must not own a Render proxy, GPU handle, backend object, or call an
  API-specific graphics interface.

## Documentation layout

- `PLANS.md` is the architecture and stage-plan map.
- `gameplay_module.md` is the detailed existing module design.
- `TODO.md` is the acceptance-oriented roadmap.
- `.plan/<stage>.md` holds a concrete stage design.
- `.spec/specs/` holds multi-stage intent; `.spec/journal/` holds factual
  implementation and validation evidence.

Do not duplicate execution history into plans or TODO entries.

## Validation

Follow [the repository validation matrix](../validation_matrix.md). Gameplay
contract changes require the focused Gameplay tests; shared public headers,
CMake boundaries, or cross-module lifetime changes require broader validation.
Runtime rendering changes are not complete without cross-backend smoke and
visual evidence.
