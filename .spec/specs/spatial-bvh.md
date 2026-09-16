# Spatial BVH

- Status: stage 1 complete (structure only); stage 2 proposed
- Owner: agent
- Parent TODO: [Mesh Proxy Reconstruction TODO](../../docs/world/mesh_proxy_TODO.md) —
  its MP2 culling and the deferred "render graph" gate are where stage 2 lands.
  Forward work also touches [Render TODO](../../docs/render/TODO.md) and the
  [Render Scene roadmap](../../docs/render/render_scene/TODO.md).

## Objective

Give the engine one shared, tested spatial acceleration structure so broad-phase
scene queries stop being O(n) linear scans, and so ray tracing has an
instance-level structure to build on later.

## Current state

Before this spec, the engine had no acceleration structure of any kind. Every
broad-phase query walked the whole scene:

- `RenderWorld` (`engine/runtime/render/render_world/render_world.h`) owns every
  `MeshProxy`, and `SceneVisibility::BuildVisibleProxies`
  (`render_world/scene_visibility.cpp`) frustum-culls them by testing all of
  them against six planes; `BuildVisibleSections` does the same per section.
- `GameplayWorld::PickActor`
  (`engine/runtime/gameplay/world/gameplay_world.cpp`) ray-casts by iterating
  every actor and calling `spatial::IntersectRayAABB`.

The scene is already uniformly described by `spatial::AABB`
(`mesh_proxy.h` carries `world_bounds`, filled by `spatial::TransformAABB` in
`primitive_component.cpp`), and `spatial/ray.h` already provides `Ray` and
`IntersectRayAABB`. That made a single AABB-oriented structure in `core/spatial`
the right shared substrate.

## Scope and non-goals

In scope (stage 1, landed):

- `engine/runtime/core/spatial/linear_bvh.h` + `linear_bvh.cpp` — `LinearBVH`
  over `AABB` primitives, with binned-SAH build, refit, nearest-ray, all-hits
  ray, and AABB overlap queries. `LinearBVH` is not a template, so its
  implementation lives in the `.cpp`; only the `IntersectRayAll` template member
  stays in the header.
- `engine/test/unit/spatial/` — a `SpatialUnitTest` target proving the structure
  against brute force.

Explicit non-goals:

- No Render, Gameplay, or Editor wiring; no new runtime command (stage 2).
- No triangle-level (BLAS) BVH, no GPU buffer packing, no TLAS.
- No `Frustum`-aware query in `spatial`: `QueryOverlap` takes an `AABB` so
  `spatial` stays free of render policy. A frustum caller composes the planes
  itself.
- No incremental insert or remove. `Refit` covers "things moved"; `Build`
  covers everything else.

## Invariants

1. `spatial` depends only on `math`. The BVH must not include a Render,
   Gameplay, Graphics, or backend type. `Spatial` is a STATIC library with
   `PUBLIC` include directories and a `PUBLIC` `Math` link, so the `.cpp`
   compiles against the same include root its consumers use. Only `Core` links
   `Spatial` directly; every other module inherits it through `Core`.
2. The structure indexes primitives by their position in the caller's array and
   stores only a permutation. Callers keep ownership of their own objects.
3. **No primitive is ever dropped.** Every index in `[0, PrimitiveCount())`
   appears exactly once in `PrimitiveOrder()`, and every stored box is finite and
   ordered so traversal cannot meet a NaN.
4. Build always terminates. Every recursion strictly shrinks its range, and
   `max_depth` is a hard cap.
5. Build is deterministic: identical input produces an identical tree on every
   run and every compiler.
6. A child node always has a higher index than its parent, which is what makes
   one reverse sweep a valid bottom-up refit.
7. Queries are const and allocate nothing.

## Stages

### Stage 1 — the structure (complete, 2026-09-14)

- [x] `LinearBVHNode`, `LinearBVHBuildOptions`, `LinearBVHRayHit`, and the
  `LinearBVH` class in `core/spatial/linear_bvh.h`, with the non-template
  implementation in `linear_bvh.cpp` and the `Spatial` target promoted from
  INTERFACE to STATIC. This is the module's first source file, and the repo
  convention is that non-template code is not header-only.
- [x] Binned SAH build over the widest centroid axis, with an equal-count
  fallback that makes coincident-centroid input terminate.
- [x] `Refit` for moved primitives with unchanged topology.
- [x] `IntersectRay` (ordered, near-child-first, pruned), `IntersectRayAll`
  (unordered visitor), `QueryOverlap` (AABB range query).
- [x] Repair of malformed boxes, with the affected indices reported.
- [x] `SpatialUnitTest` with 25 cases, the core being property tests against
  brute-force scans.

### Stage 2 — render culling wiring (investigated 2026-09-14, declined)

Measured, then not wired. The measurement is the reason, so it is recorded here
rather than left as a decision without evidence.

`LinearBVH::QueryFiltered` was added for this stage and then **removed again**:
with the frustum use case declined it had no consumer at all, and a general
region-query entry point nothing calls is speculative surface. Ray tracing would
use `IntersectRay`/`IntersectRayAll`, not this. `QueryOverlap` keeps its own
traversal. The API is back to exactly what has a use.

Stage 2 as originally proposed assumed a proxy-count threshold above which a BVH
would win. Two measurements over synthetic packets matching the real
`VisibleMeshSection` stride (Debug, this machine) show there is no such
threshold:

| packets | linear scan | gather | build | query | build+query | ratio |
|---|---|---|---|---|---|---|
| 738 (sponza) | 29.5 us | 22.2 | 1629.8 | 44.4 | 1674 | 56.7x |
| 2048 | 84.6 | 63.8 | 4754.9 | 104.0 | 4859 | 57.4x |
| 8192 | 338.9 | 251.6 | 21352 | 354.9 | 21707 | 64.1x |
| 32768 | 1357 | 1024 | 93585 | 1345 | 94930 | 69.9x |

A per-call build is 57-70x the linear scan and the ratio *worsens* with scale,
because the SAH build costs about 2.2 us per packet against the scan's 0.04 us.

Nor does a free tree fix it. Traversal alone, 8192 packets, as the frustum
shrinks:

| surviving | linear | query | ratio |
|---|---|---|---|
| 31.9% | 358 us | 387 us | 1.08x (worse) |
| 5.1% | 327 | 78.0 | 0.24x |
| 1.0% | 294 | 21.4 | 0.07x |
| 0.1% | 266 | 8.8 | 0.03x |

Traversal wins only below roughly 10% survival, because at high survival it does
*more* plane tests than the flat scan: it tests the frustum at every node and
every primitive, plus stack traffic. Sponza's G-buffer survival is 285/738 =
39%, so a cached tree would still lose there.

Scale of the stakes: sponza's entire G-buffer filter is 29.5 us/frame, about
0.18% of a 16.6 ms frame. The measured `section_packet_build_cpu_ms` is 2.87 -
over 100x larger - and it lives in `BuildSectionCandidates`, which the BVH
cannot help because that pass deliberately takes no frustum (the shadow
schedulers need every potential caster).

Revisit only if a scene appears with low survival at high count, and then with
the cached-tree design below, not a per-call build.

- [x] ~~Replace the linear scans in `SceneVisibility::BuildVisibleProxies` and
  `BuildVisibleSections` with BVH traversal~~ - `BuildVisibleProxies` and the
  `MeshProxy` overload of `BuildVisibleSections` turned out to be **dead in
  production** (only unit tests call them), so accelerating them would have
  accelerated nothing. The live path is `BuildSectionCandidates` ->
  `FilterVisibleSections`, and it is not a win per the measurements above.
- [ ] Back `GameplayWorld::PickActor` with `IntersectRay`. Still open, and the
  one place a single-query structure has an obvious shape to win: picking is one
  ray against N actors, not N queries against a rebuild.
- [ ] If revisited: a persistent, content-stamped tree shared across the G-buffer
  filter and every per-light shadow filter in `DeferredRenderer`, keyed on the
  proxy set rather than rebuilt per call. This is the only formulation that
  reaches the low-survival regime where traversal wins. Designed, not authorized:
  [R2 — adaptive render spatial index](../../docs/render/.plan/R2.md).
- [ ] Validate through the runtime command registry with a checked-in startup
  fixture, not by compilation alone.

### Stage 3 — ray tracing groundwork (proposed, not designed)

- [ ] Triangle-level BLAS over `data::MeshData` indices, sourced from the asset
  side, since Render only exposes per-section ranges and bounds today.
- [ ] A TLAS over instances, reusing this structure, plus GPU layout and
  all-hits traversal for shadow rays.

## Acceptance criteria

- [x] Every primitive is indexed exactly once; leaf ranges tile the order array.
- [x] Nearest-hit and all-hits ray queries match a brute-force scan using
  `IntersectRayAABB` over randomized scenes and rays.
- [x] Overlap queries match a brute-force AABB overlap scan.
- [x] Build terminates and splits evenly for coincident centroids.
- [x] Identical input produces an identical tree.
- [x] Refit immediately after build is a no-op; refit after moving primitives
  keeps topology and restores brute-force agreement.
- [x] Axis-parallel rays on a slab plane still hit, covering the `0 * inf = NaN`
  case.
- [x] Malformed boxes are repaired and reported, never dropped.

## Validation plan

```powershell
cmake --build build --config Debug --target SpatialUnitTest
ctest --test-dir build -C Debug -R LinearBVHTest --output-on-failure
```

Adding the test subdirectory requires a CMake re-configure; the Visual Studio
generator re-runs it from the build step, or `cmake -S . -B build` forces it.

Stage 1 has no consumer, so compilation plus the unit suite is the whole
evidence: there is no runtime or visual path to exercise, and no screenshot
capture is warranted. Stage 2 is where the runtime fixture and capture
requirement applies.

## Risks

- `IntersectRayAABB` in `spatial/ray.h` treats a direction component with
  `|d| <= 1e-7f` as exactly parallel, while the BVH's node slab test treats it
  as a genuine (enormous) `t`. The two can disagree about a *near*-parallel ray.
  The disagreement is safe in the direction that matters: the epsilon branch only
  widens what it accepts, and a node test cannot reject a box whose origin is
  strictly inside that slab, so the BVH never prunes a primitive that
  `IntersectRayAABB` would have accepted. The primitive test remains
  authoritative. Verified by the near-parallel cases in the suite; a future
  change to the epsilon must re-check this.
- The slab test's NaN absorption depends on `std::min`/`std::max` returning their
  first argument when the second is NaN. Every mainstream implementation does,
  and the argument ordering is commented at the definition, but this is
  implementation behaviour rather than a standard guarantee. `std::fmin`/
  `std::fmax` would make it standard-defined at the cost of real calls in the
  traversal loop.
- `Refit` cannot rebalance, so a scene that drifts persistently degrades toward
  a linear scan of leaves. Callers must rebuild periodically.
