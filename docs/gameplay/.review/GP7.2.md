# GP7.2 Review — Static-Mesh Level Instantiation and Rollback

- Task ID: `GP7.2`
- Plan: [GP7.2 — Static-Mesh Level Instantiation and Rollback](../.plan/GP7.2.md)
- Parent roadmap: [Gameplay TODO](../TODO.md#gp7--level-asset-and-startup-scene-migration)
- Review status: passed; no open GP7.2 findings
- Latest review: 2026-09-01

## Scope and baseline

The review covers the GP7.2 Runtime-owned `LevelInstance`, static-mesh Asset
preflight, Gameplay Actor creation, rollback/unload, destroyed-Actor
reclamation, RuntimeContext lifetime ordering, and focused tests.

GP7.3 work is currently layered into the same `LevelInstance` files. Light,
camera, environment-source, and mixed-object behavior belongs to the separate
[GP7.3 plan](../.plan/GP7.3.md) and is excluded from the GP7.2 verdict except
where it could regress the shared GP7.2 transaction.

## Evidence inspected

- `engine/runtime/level/level_instance.h`
- `engine/runtime/level/level_instance.cpp`
- `engine/runtime/gameplay/world/gameplay_world.h`
- `engine/runtime/gameplay/world/gameplay_world.cpp`
- `engine/runtime/runtime_global_context.h`
- `engine/runtime/runtime_global_context.cpp`
- `engine/runtime/level/CMakeLists.txt`
- `engine/test/unit/runtime_level/runtime_level_test.cpp`
- `engine/test/unit/gameplay/gameplay_world_test.cpp`
- GP7.2 plan, GP7 spec, Gameplay module documentation, status, journal, and
  validation matrix

## Review round 1 — 2026-09-01

### F1 — exceptional exits bypassed rollback

- Priority: P2
- Original location: `engine/runtime/level/level_instance.cpp`, commit phase
- Status: resolved

The original commit phase used a plain vector of created handles. An exception
from the injectable Actor factory or from the post-creation authored-ID map
allocation could leave active Actors and copied Render sources while the
`LevelInstance` still reported `Empty`.

Resolution evidence: the commit now installs an RAII scope guard immediately
after creating the temporary handle set. Every return or exception after that
point destroys any created environment source and rolls Actors back in reverse
order. The guard is dismissed only after the active map, creation order,
environment handle, level ID, and active state are published. The focused test
`RollsBackWhenActorFactoryThrowsAfterCreatingAnActor` verifies the exceptional
path and immediate retry.

### F2 — required negative preflight cases were absent

- Priority: P2
- Original location: `engine/test/unit/runtime_level/runtime_level_test.cpp`
- Status: resolved

The original suite covered only an out-of-range dependency index. It did not
exercise the typed failures required by the GP7.2 plan or destructor cleanup.

Resolution evidence: the focused suite now covers invalid level identity,
invalid model payload, missing mesh geometry, invalid mesh identity/data and
bounds, invalid material payload, ordinary and exceptional rollback, stale
lookup, active replacement rejection, Asset residency, and destructor-driven
source retirement.

## Review round 2 — 2026-09-01

No open GP7.2 correctness, ownership, or acceptance findings remain.

The shared transaction still preserves the GP7.2 invariants:

- Asset owns dependency identity and CPU payloads;
- Runtime owns active level-instance state and authored-ID mappings;
- GameplayWorld remains the sole Actor owner;
- rollback and unload destroy Actors in reverse creation order and reclaim
  before immediate retry;
- RuntimeContext destroys the instance before GameplayWorld and RenderSystem;
- the instance does not load or unregister Assets and owns no GPU resource.

## Validation

Current-tree validation on 2026-09-01:

- `.\tools\kp.ps1 build RuntimeLevelTest` — passed
- `.\tools\kp.ps1 build GameplayUnitTest` — passed
- `.\tools\kp.ps1 build RuntimeLib` — passed
- `ctest --test-dir build -C Debug -R "RuntimeLevelTest|GameplayWorldTest" --output-on-failure`
  — passed 37/37 (18 RuntimeLevel tests and 19 GameplayWorld tests)

The broader GP7.3 Render and environment-source changes in the working tree
require their own task review and validation record. No runtime visual capture
is required for GP7.2 because it remains outside the live startup path.
