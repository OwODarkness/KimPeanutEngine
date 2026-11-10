# Runtime settings and subsystem-owned settings pages

- Status: active
- Owner: Runtime settings infrastructure; domain modules own adapters
- Parent TODO: [Settings roadmap](../../docs/settings/TODO.md)
- Architecture: [Settings plans](../../docs/settings/PLANS.md)

## Objective

Let a module expose validated configuration through a central service and a
shared settings window without adding subsystem preferences to Engine. Prove
the boundary first with headless contracts, then Rendering startup/live settings.

## Current state

SET1 implements the headless registry, layered stores, and provider lifecycle;
its explicit write-failure injection acceptance remains open. SET2 composes the
service before startup, resolves Render settings before backend creation, and
routes Path Tracing, SSAO, TAA and Height Fog edits through Render's safe-frame
provider. Existing CLI flags remain explicit session overrides. The GUI Settings
window is SET3. Host-specific launch inputs remain owned by their configured
hosts.

## Scope and non-goals

SET1 delivers the registry/store; SET2 integrates Rendering; SET3 adds the
generic Editor window; SET4 proves a second domain and explicit migrations;
SET5 addresses remaining domain-owned startup configuration. Their acceptance
criteria live in the roadmap, with concrete SET1/SET2 designs linked there.

Implementation does not include a general service locator, a new reflection
backend, live plugin reload, cross-subsystem transactions, direct ImGui mutation
of runtime objects, or automatic migration of every existing config file.

## Invariants

- Domain schemas/defaults and typed active state remain domain-owned.
- Settings resolves configuration before consuming systems initialize.
- Runtime algorithms do not read generic registry entries in hot loops.
- Editor consumes copied facts and requests edits; no live mutable object access.
- Request acceptance, application, restart intent and persistence are distinct.
- Providers unregister before teardown; stale/late results cannot target a new
  binding. RuntimeSettings does not depend on concrete domains or Editor.
- Explicit CLI input, existing command names and API fallback remain compatible.
- Unknown optional-module preferences survive save and absent-module runs.

## Stages

Follow [SET1](../../docs/settings/.plan/SET1.md),
[SET2](../../docs/settings/.plan/SET2.md), and the
[remaining roadmap](../../docs/settings/TODO.md). Write SET3/SET4/SET5 concrete
designs from the accepted earlier contracts before implementing those stages.

## Acceptance criteria

- [ ] SET1 write-failure and full headless acceptance pass.
- [x] SET2 removes RT/PT Engine forwarding and proves startup/live behavior.
- [ ] SET3 exposes metadata-driven pages with correct apply/save feedback.
- [ ] A second domain registers without changing Engine or registry algorithms.
- [ ] Relevant runtime, visual, compatibility and shutdown evidence is recorded.

## Validation plan

This documentation task: Level 0, local link/anchor checks, placement evidence
and `git diff --check`. Implementation: levels 1/2 for headless contracts;
levels 3/4 for shared startup, rendering/Editor and CMake changes as required by
the [validation matrix](../../docs/validation_matrix.md). Builds and runtime
checks are future work, not evidence of this plan.

## Risks and open questions

The first Render adapter must audit initialization-only PT resources and add
applied-state acknowledgement. Exact C++ signatures and bounded queue sizes
are implementation decisions to record in SET1. Audio and legacy Editor/TTS
file migrations need their own consumers and acceptance before being specified.
No design depends on exposing direct EnTT access or copying another engine's
full settings framework.
