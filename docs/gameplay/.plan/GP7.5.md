# GP7.5 — Runtime Evidence and GP7 Handoff

- Status: complete (2026-09-02)
- Parent spec: [Gameplay Level Asset (GP7)](../../../.spec/specs/gameplay-level-asset.md)
- Parent roadmap: [Gameplay TODO](../TODO.md#gp7--level-asset-and-startup-scene-migration)
- Depends on: [GP7.4](GP7.4.md)
- Affected owners: Asset, Runtime, Gameplay, Render
- Deliverable: final GP7 evidence, risk statement, and status handoff

## Outcome

Close GP7 through an independent audit of the landed level-asset startup path.
Prove that startup, failure rollback, normal shutdown, source retirement, Asset
dependency lifetime, and the three authored validation levels behave correctly
on the rebuilt Vulkan and OpenGL runtimes. Record factual results in the GP7
journal, then mark GP7 complete only if every required lane passes or has an
explicitly accepted blocker.

GP7.5 is verification and handoff, not another architecture stage. It may add
missing tests or fix defects discovered by the audit, but it must not add live
level switching, a `WorldResource`, asynchronous activation, a new Render
abstraction, or the proposed command-line startup-level override. That override
has its own [post-GP7 plan](STARTUP_LEVEL_OVERRIDE.md).

## Entry baseline

GP7.1–GP7.4 are landed. GP7.4 already reports:

- strict Bootstrap V2 with `level/pbr_showcase.level` selected;
- complete Level/Material authored dependency closure;
- Runtime-owned level instantiation and authored-camera possession;
- startup ready/commit-or-abort synchronization and failure cleanup;
- removal of bootstrap-authored Render scene plumbing;
- PBR, point-shadow, and spot-shadow level fixtures;
- full Debug build, 222/222 CTest, Vulkan/OpenGL `GraphicsSmoke`, and initial
  dual-backend captures.

Treat that as prior implementation evidence, not as automatic GP7.5 success.
GP7.5 reruns the final matrix from the current worktree and checks the evidence
for internal consistency. Do not rewrite the earlier journal entry if a result
changes; append a dated correction and the new result.

## Audit boundary

```text
Bootstrap V2 -> Asset Level identity/dependency closure
                        |
                        v
             Runtime startup transaction
                        |
              LevelInstance + controller
                        |
                Gameplay-owned Actors
                        |
                  copied sources
                        v
                Render registries/passes
                        |
                  Vulkan / OpenGL

shutdown: Render frame stop
       -> level unload/source retirement
       -> Gameplay destruction
       -> Render/GPU teardown
       -> Asset dependency release audit
```

The audit must preserve these ownership facts:

- Asset owns CPU identities and dependency edges, not Actors or GPU state.
- Runtime owns the active level instance and startup/shutdown ordering.
- GameplayWorld owns Actors, components, and the local controller.
- Render owns copied source registries, material/environment resolution, pass
  state, and GPU-facing resources.
- Graphics/RHI owns backend execution and safe GPU destruction.

## GP7.5.1 — Static contract audit

Review the final diff and live call paths before running expensive validation:

- `config/bootstrap.json` contains only Bootstrap V2 and selects
  `level/pbr_showcase.level`;
- no bootstrap `assets`, `scene`, transforms, materials, environment, camera,
  or light authoring remains;
- Runtime passes only a ready `KPAT_Level` AssetID into `LevelInstance`;
- Render does not receive a level path/LevelResource or call authored
  `LoadSync` from material/environment resolution;
- material shader and texture references resolve through typed dependency
  indices;
- all three checked-in fixture paths normalize beneath the Asset root;
- no shared contract exposes Actor pointers, Render proxies, GPU handles, or
  Vulkan/OpenGL types;
- teardown still destroys `LevelInstance`, then GameplayWorld, then Render.

Any surviving transitional path is a GP7 blocker, not a documentation-only
risk.

## GP7.5.2 — Focused lifecycle and dependency proof

Run the existing focused suites first. Add a test only where the end-to-end
claim is not already covered.

### Startup and rollback

- valid startup commits one active level and possesses its preferred enabled
  camera;
- camera-free, stale, wrong-type, factory, environment-registration,
  controller, and possession failures leave no active level, Actor mapping, or
  live source token;
- exceptions after partial Actor creation roll back in reverse order and permit
  immediate retry;
- render-start failure and game-start abort wake both sides of the startup
  handshake and leave no joinable thread;
- `Engine::Clear` remains terminal and idempotent for its supported lifecycle.

### Source retirement

- unload retires the environment before level Actors;
- Actor destruction retires mesh, camera, and light sources once;
- registry drain invalidates stale source handles and returns environment and
  camera selection to their safe fallback state;
- shutdown clears pending and active source commands before Render resolver and
  backend teardown.

### Asset lifetime

- a loaded level prevents direct dependency unregister while its Asset edge is
  live;
- `LevelInstance::Unload` does not itself unregister Asset identities;
- after runtime ownership is gone and the level Asset is explicitly
  unregistered, dependencies can be released according to remaining reverse
  references;
- repeated model/material/texture references preserve one identity and no
  stale dependency index;
- a failed dependency load does not register a partial parent level or leave a
  reverse edge.

If the existing focused tests prove each statement independently but no test
crosses the complete load -> instantiate -> unload -> unregister sequence, add
one bounded RuntimeLevel/Asset integration test. Do not expose private GPU
state or add production lifecycle APIs only for inspection.

## GP7.5.3 — Full rebuilt validation

After focused tests pass, run sequentially:

1. CMake configure if the build graph or generated project is stale;
2. focused Asset, Bootstrap, RuntimeStartup, RuntimeLevel, Gameplay, source
   registry, material resolver, and RenderSystem tests;
3. `cmake --build build --config Debug`;
4. `ctest --test-dir build -C Debug --output-on-failure`;
5. rebuilt `GraphicsSmoke` on Vulkan;
6. rebuilt `GraphicsSmoke` on OpenGL.

Do not run concurrent CMake/MSBuild jobs. Capture the first source failure. If
the Windows SDK or another host permission blocks a build, record the exact
environment error separately and do not treat an older binary as new source
evidence.

Run with available Vulkan validation/debug output and OpenGL diagnostics. The
handoff is blocked by a live-source, descriptor, synchronization, or GPU
lifetime error even when the executable exits zero.

## GP7.5.4 — Dual-backend fixture captures

Until the separate startup-level override is implemented, select fixtures by a
controlled temporary edit of only `config/bootstrap.json`'s `startup_level`.
For each selection, verify the parsed/logged startup path before accepting any
image. Never change level contents or Runtime C++ to make a capture pass.

Capture fresh files under `save/screenshots/validation/`:

| Level | Required views per backend | Suggested prefix |
|---|---|---|
| `pbr_showcase.level` | `scene_color` | `gp7-5-pbr-{api}` |
| `point_shadow_validation.level` | `scene_color`, `point_shadow_depth`, `point_shadow_visibility` | `gp7-5-point-{api}` |
| `spot_shadow_validation.level` | `scene_color`, `spot_shadow_depth`, `spot_shadow_visibility` | `gp7-5-spot-{api}` |

For every capture:

- launch the rebuilt Runtime with the chosen API and local agent transport;
- wait until startup and the relevant shadow/material pipelines are ready;
- submit `capture.screenshot`, poll its request to terminal success, and use
  the returned output path;
- record file dimensions and a SHA-256 hash;
- inspect the actual image rather than treating export success as visual proof;
- preserve captures made by other work.

Visual inspection must cover:

- PBR object count, transforms, authored camera, visible environment, material
  response, and IBL parity;
- point caster/receiver silhouette, non-uniform visibility, six-face
  orientation/tile continuity, and absence of a competing shadow source;
- spot cone coverage, caster/receiver silhouette, non-uniform visibility, and
  absence of a competing shadow source;
- equivalent scene composition and shadow meaning across Vulkan/OpenGL;
- expected diagnostic limitations, such as depth visualization encoding,
  stated explicitly rather than hidden.

Byte-identical PNGs are not required across APIs. If visual output differs,
determine whether it is expected encoding/rasterization variance or a semantic
defect. Fix semantic defects and rerun the affected focused, smoke, and capture
lanes.

After all fixture captures, restore and verify:

```json
{
  "version": 2,
  "startup_level": "level/pbr_showcase.level"
}
```

The restored config is part of GP7.5 acceptance.

## GP7.5.5 — Evidence and handoff

Append one dated GP7.5 section to
`.spec/journal/2026-09-01-gameplay-level-asset.md` containing:

- exact commands and result counts;
- environment failures distinguished from source failures;
- fixture/backend/view capture paths and hashes;
- concise visual findings for each fixture;
- validation/debug-layer findings;
- defects found, fixes made, and evidence rerun after each fix;
- checks skipped and why;
- remaining risks explicitly deferred beyond GP7.

Only after the evidence is complete:

- check GP7.5 in `docs/gameplay/TODO.md`;
- mark the parent GP7 spec complete and check its acceptance criteria;
- update `docs/status.md` with one concise GP7 completion summary and links;
- update Gameplay module status from GP7.5 proposed to GP7 complete;
- leave detailed commands and chronology only in the journal.

Do not mark GP7 complete when a required capture was merely exported but not
inspected, when config was not restored, or when validation used stale
binaries.

## Acceptance criteria

- [x] Static audit finds no legacy bootstrap scene/preload path, authored
  Render load, ownership leak, or backend-type leak.
- [x] Startup and every tested failure path leave an all-or-nothing active
  level and terminate both thread handshakes safely.
- [x] Unload/shutdown retire environment, camera, light, and mesh sources before
  their owning systems disappear; stale handles are rejected.
- [x] Level dependency edges block premature release and permit release after
  runtime ownership and the level Asset edge are removed.
- [x] Focused tests, full Debug build, complete CTest, and Vulkan/OpenGL
  `GraphicsSmoke` pass from the current source.
- [x] Fresh PBR, point-shadow, and spot-shadow captures succeed and are visually
  inspected on both backends.
- [x] `config/bootstrap.json` is restored to `level/pbr_showcase.level`.
- [x] The journal contains exact final evidence, fixes, skipped checks, and
  remaining risks.
- [x] TODO, parent spec, Gameplay status, and project status consistently mark
  GP7 complete only after all previous criteria are satisfied.

## Deferred beyond GP7

- command-line startup-level override and reusable launch-option parsing;
- live level unload/replacement, transition barriers, and streaming;
- multiple active levels or a `WorldResource`;
- editor level browser/selection and save-back;
- asynchronous Asset loading/progress/cancellation;
- new point-shadow cubemap/subresource contracts without measured need.
