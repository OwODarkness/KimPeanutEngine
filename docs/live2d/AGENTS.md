# Live2D Module Documentation Guide

Read the repository [agent contract](../../AGENTS.md), this guide, the
[Live2D architecture map](PLANS.md), the [Live2D roadmap](TODO.md), and the
exact stage plan under [`.plan/`](.plan/) before changing the Live2D module or
its documentation.

## Boundaries

- Live2D is an optional module under `engine/module/live2d`; Runtime, Asset,
  Render, Graphics, and Editor must continue to build when it is disabled.
- Asset owns runtime identity, cache/dependency registration, and immutable CPU
  payload lifetime. Asset exposes generic registration contracts and must not
  include a Live2D header, name a Live2D type, or construct a Cubism object.
- The Live2D importer owns `.model3.json` source-closure discovery and native
  Live2D product generation. Runtime loading never imports or mutates source
  content.
- A Live2D model asset is immutable shared source data. A Live2D model instance
  owns mutable parameters and deformed drawable state. Never put per-character
  motion, expression, pose, or physics state in the shared Asset payload.
- Live2D Render owns Live2D draw policy and render-ready state. Graphics/RHI
  owns GPU buffers, images, pipelines, synchronization, and safe destruction.
- Common headers must not expose `Vk*`, `GL*`, Cubism renderer backend types,
  or proprietary Core implementation details.
- Cubism Framework/Core startup is process-scoped. Initialize it once before
  creating assets or instances and dispose it only after every instance and
  renderer has been destroyed.
- Treat Live2D licensing as a release gate. Do not commit Cubism Core binaries,
  model data, or SDK sample content without verifying the applicable license
  and redistribution terms.

## SDK policy

- The planned baseline is the latest stable Cubism SDK for Native version
  recorded in [PLANS.md](PLANS.md). Do not silently move to an alpha, beta, or
  later stable release.
- Keep the selected Cubism Framework revision and downloaded Cubism Core
  package on the same release line.
- SDK integration is enabled by default for engine development and can be
  disabled explicitly through CMake. An SDK-free build must use
  `KPENGINE_ENABLE_LIVE2D=OFF`; an enabled build with an incomplete SDK must
  fail early with a precise diagnostic.
- Prefer an external SDK root or installer/cache location. Do not work around
  licensing by fetching or checking in proprietary Core binaries.
- The official renderer samples are conformance references, not the production
  renderer architecture. Production drawing goes through KimPeanutEngine's
  API-neutral Graphics contract.

## Documentation layout

- `PLANS.md` owns architecture, current-state analysis, durable decisions, and
  reference conclusions.
- `TODO.md` owns the acceptance-oriented roadmap.
- `.plan/L2D*.md` owns concrete stage designs.
- [`.spec/specs/live2d-v1-rendering.md`](../../.spec/specs/live2d-v1-rendering.md)
  owns the cross-stage V1 execution contract.
- `.spec/journal/` records implementation and validation evidence after work
  begins. Do not pre-write planned work as landed fact.

## Stage workflow

1. Resolve the exact `L2D` stage ID from [TODO.md](TODO.md).
2. Read the parent [V1 spec](../../.spec/specs/live2d-v1-rendering.md) and the
   matching `.plan` document.
3. Preserve the dependency direction and non-goals of that stage. Do not absorb
   animation, expression, lip-sync, physics, or interaction work into V1.
4. Compare visible output with an official Cubism sample renderer when render
   behavior changes. Compilation does not prove draw order, alpha, blend, or
   clipping correctness.
5. Record factual commands, results, skipped checks, SDK version, and fixture
   license status in the Live2D journal.

## Validation

- Asset registry changes require focused type/loader/importer registration,
  collision, payload type, dependency, unloading, and concurrent-load tests.
- Native Live2D product changes require deterministic round-trip, malformed
  input, bounds, path-escape, missing-dependency, and version tests.
- SDK lifecycle changes require repeated initialize/shutdown and multi-instance
  tests under the available memory diagnostics.
- Graphics contract changes require `GraphicsContractTest` plus OpenGL and
  Vulkan `GraphicsSmoke`.
- V1 is complete only after a dedicated Live2D viewer renders the same model on
  OpenGL and Vulkan, survives resize, captures output, and shuts down without
  validation errors or leaked GPU/module state.
