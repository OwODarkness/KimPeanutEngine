# Runtime Reflection Module

- Status: proposed
- Owner: user + Codex
- Parent TODO: [Reflection Module TODO](../../docs/reflection/TODO.md)
- Architecture: [Reflection Module Plans](../../docs/reflection/PLANS.md)

## Objective

Add an engine-owned Runtime Reflection module backed by EnTT 3.16.0, then use
it to build a thread-safe Gameplay Actor inspector. Preserve a clean consumer
API while allowing module-owned registration code to use a thin EnTT-specific
registrar.

## Current state

EnTT is vendored and its raw ECS/meta API has a narrow integration test. The
engine has no reflection lifecycle owner, stable descriptor/value vocabulary,
Gameplay registration, Actor enumeration snapshot, component-instance
identity, property edit queue, or Actor panel. Gameplay owns mutable Actors on
the game thread; Editor/ImGui runs on the render thread.

## Scope

- Runtime Reflection target and lifecycle owner.
- Engine-neutral catalog, descriptors, values, diagnostics, and controlled
  live-object access interface.
- Explicit EnTT context plus thin registration adapter.
- Module-owned Gameplay registration through behavior-safe accessors.
- Copied Actor/component snapshots and queued property edits.
- World Outliner and Actor Inspector consuming only clean interfaces.

## Non-goals

- Full concealment of EnTT from module registration `.cpp` files.
- EnTT types in Editor or normal Gameplay headers.
- Direct render-thread access to `GameplayWorld`, Actor, or components.
- ECS migration, prefab construction, generalized serialization, script
  binding, undo/redo, multi-select, or hot reload in the initial delivery.

## Invariants

- Reflection owns metadata and backend lifetime, never reflected object memory.
- Registering modules own knowledge of their C++ types; dependency direction
  never becomes Reflection -> Gameplay/Editor/Render/Asset.
- The public consumer API exposes no EnTT, ImGui, Vulkan, or OpenGL type.
- Registration is explicit and transactional; publication occurs only after a
  successful immutable freeze.
- Live Gameplay access is game-thread-only and uses behavior-preserving
  getters/setters.
- Editor observes copied snapshots and emits value-only commands.
- Actor and component identities are revalidated at edit application time.
- Reflection does not silently define an Asset serialization schema.

## Stages

1. **RF1 — contracts and adapter.** Follow the
   [RF1 plan](../../docs/reflection/.plan/RF1.md).
2. **RF2 — Gameplay registration.** Register the minimum values and component
   properties, proving setter side effects and owner-thread access.
3. **RF3 — editor bridge.** Add component-instance identity, immutable
   snapshots, bounded edit commands, result diagnostics, and stale-target
   handling.
4. **RF4 — Actor tooling.** Add World Outliner and Inspector panels with
   metadata-driven widgets and dual-backend runtime proof.
5. **RF5 — extensions.** Scope undo/redo, save-back, scripting, and hot reload
   independently after the first inspector is stable.

## Acceptance criteria

- [ ] Reflection has a dedicated Runtime module and explicit EnTT context.
- [ ] Consumers query engine descriptors without including EnTT.
- [ ] Gameplay registration remains in Gameplay-owned units.
- [ ] At least transform, mesh, light, and camera properties have validated
  registration and owner-thread access coverage.
- [ ] Actor/component snapshots contain no live object pointer.
- [ ] Edits are queued, revalidated, and applied on the game thread.
- [ ] The Actor panel has no direct Gameplay or EnTT dependency.
- [ ] Focused contract tests, affected Runtime/Editor tests, and Vulkan/OpenGL
  smoke evidence pass at the appropriate stages.
- [ ] Each landed stage records factual implementation and validation evidence
  in a journal before its roadmap/status item is closed.

## Validation plan

- RF1: configure/build and run focused `ReflectionUnitTest`.
- RF2: focused Reflection and Gameplay tests; existing component/source
  behavior must remain valid.
- RF3: Reflection, Gameplay, Runtime threading/lifecycle, queue-bound, stale
  handle, and teardown tests.
- RF4: affected Editor lifecycle tests plus Vulkan and OpenGL runtime smoke;
  visually confirm selection and one property edit when capture can show it.
- Run the full Debug build and CTest whenever shared public Runtime headers or
  CMake dependency boundaries make the affected scope uncertain.

## Risks and open questions

- Stable component-instance identity does not exist yet and must coexist with
  duplicate component types and deferred Actor reclamation.
- The first `ReflectionValue` set must remain narrow enough to avoid becoming a
  generic serialization value tree prematurely.
- Catalog descriptor lifetime must remain valid across concurrent immutable
  reads and orderly shutdown.
- Setter failure and edit acknowledgement need one consistent structured
  result; optimistic Editor mutation is not acceptable.
- Dynamic registration and DLL hot reload require generation-aware metadata
  handles and are explicitly deferred.
