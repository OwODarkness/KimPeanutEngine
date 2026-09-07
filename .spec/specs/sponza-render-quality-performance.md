# Sponza Render Quality and Performance

**Status:** Stage 0 instrumentation landed; baseline evidence pending

**Owner:** Asset / Resource / Render / Graphics

**Parent roadmap:** [Render TODO](../../docs/render/TODO.md)

**Design:** [issue-9.7 plan](../../docs/render/.plan/issue-9.7.md)

**Review:** [issue-9.7 review](../../docs/render/.review/issue-9.7.md)

**Journal:** [execution journal](../journal/2026-09-07-sponza-render-quality-performance.md)

## Objective

Eliminate Sponza's texture-minification speckle and bring its fixed reference
scenario to a measured p95 frame time of at most 16.67 ms, without obscuring
the defect through blur or unmeasured global quality reductions.

## Scope

This work includes performance instrumentation, explicit semantic mip
artifacts, cross-backend subresource upload, bounded native texture products,
fence-safe descriptor arenas, persistent material bindings, section bounds and
culling, directional-shadow reuse, and evidence-driven pass tuning.

The resolved black-frame/uniform-exhaustion defect, render-graph adoption,
virtual texturing, transparent sorting, and unrelated renderer refactors are
out of scope.

## Invariants

- Asset and offline import retain identity, decoding, semantic metadata, native
  product publication, model section bounds, and CPU asset lifetime.
- Resource processing creates render-ready CPU artifacts and owns no GPU
  object.
- Render owns sampling, visibility, binding, pass, and invalidation policy.
- Graphics owns backend upload, descriptors, synchronization, timestamps, and
  safe GPU-resource retirement.
- Common interfaces expose no backend-native types.
- Declared mip levels always have valid data; sampler LOD never exceeds the
  populated chain.
- Descriptor reset, cache eviction, and shadow reuse never cross an unsafe
  fence or stale dependency revision.

## Stages and gates

1. **Baseline:** fixed scenario plus CPU/GPU/present, draw, descriptor, texture,
   and shadow telemetry on Vulkan and OpenGL.
2. **Mip correctness:** semantic generation, explicit subresources, dual-backend
   upload, compatible sampler LOD, and deterministic tests.
3. **Texture budget:** profile dimensions, supported compressed formats,
   fallback, product validation, and resident-byte accounting.
4. **Descriptor lifetime:** frame-slot arenas, stable material bindings, and
   fence/deferred-destruction tests.
5. **Visibility/shadows:** section bounds/culling and dependency-stamped static
   shadow reuse.
6. **Measured tuning and closure:** retain depth/pre-pass, ordering, PCF, or
   post-AA changes only when timings and captures justify them; complete full
   build/test and dual-backend runtime evidence.

No stage may claim a performance win from FPS alone. Record p50/p95 timings,
scenario, hardware, build, validation state, present mode, and capture paths in
the journal.

## Acceptance

- Diagnostic material captures are spatially and temporally stable.
- Both backends upload and sample complete, semantically correct mip chains.
- The exact Sponza texture closure and resident bytes are reported and meet the
  plan's recorded ceiling.
- Steady frames create no descriptor pool per draw.
- Section and shadow counters prove hidden-work rejection and unchanged-shadow
  reuse without stale output.
- The fixed performance scenario meets p95 ≤16.67 ms after warm-up on the
  reference machine, with no regression in existing PBR/capture/lifetime tests.

## Validation level

This is an L4 cross-module runtime change. Follow the validation matrix for
changed Resource, Render, common Graphics, Vulkan, and OpenGL paths: focused
unit/contract tests, Debug build and CTest, both backend smoke suites, fresh
runtime captures, visual inspection, and a separate non-validation performance
run.
