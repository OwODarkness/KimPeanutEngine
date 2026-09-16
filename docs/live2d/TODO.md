# Live2D Module Roadmap

**Status: active.** Keep architecture in [PLANS.md](PLANS.md), stage design in
`.plan/`, and implementation or validation history in
[`../../.spec/journal/`](../../.spec/journal/). The cross-stage V1 contract is
the [live2d-v1-rendering spec](../../.spec/specs/live2d-v1-rendering.md).

## Current focus

- [ ] **L2D8 — semantic emotion and body-language policy** ([plan](.plan/L2D8.md)).
  Map application intent to authored motion/expression combinations through a
  data-driven controller. Expose an SDK-free state snapshot and transition
  record for runtime and viewer diagnostics. This is the missing high-level
  emotion layer; speech bubbles remain a separate presentation channel.
  L2D8.0 and L2D8.1 are complete; L2D8.2 is the next implementation stage.
- [ ] **L2D9 — speech integration.** Add lip-sync inputs and optional TTS/audio
  coupling through narrow interfaces; Live2D must not depend on a provider.
- [ ] **L2D10 — engine/editor integration.** Add gameplay composition,
  inspector, reimport/hot reload, packaging, and command/capture support.
- [ ] **L2D11 — measured performance.** Profile first, then choose targeted
  streaming, batching, or update-rate work from measured bottlenecks.

## Landed stages

- [x] **L2D1 — Cubism SDK integration** ([plan](.plan/L2D1.md),
  [journal](../../.spec/journal/2026-09-08-live2d-l2d1.md)). Optional,
  version-pinned SDK boundary with SDK-off builds.
- [x] **L2D2 — asset integration** ([plan](.plan/L2D2.md),
  [journal](../../.spec/journal/2026-09-08-live2d-l2d2.md)). Module-owned
  type, loader, importer, and dependency closure.
- [x] **L2D3 — offline import command**
  ([journal](../../.spec/journal/2026-09-08-live2d-l2d3.md)). Deterministic
  `.live2d` products with atomic publication and collision checks.
- [x] **L2D4 — generic render submission** ([plan](.plan/L2D4.md),
  [journals](../../.spec/journal/2026-09-09-live2d-l2d4-0.md)). SDK-free
  extraction, generic geometry/mask passes, and cross-backend hardening.
- [x] **L2D5 — viewer and V1 evidence** ([plan](.plan/L2D5.md),
  [closeout journal](../../.spec/journal/2026-09-12-live2d-l2d5-4.md)). Host
  capture, resize/shutdown evidence, and V1 gate disposition.
- [x] **L2D6 — authored motion and expression playback**
  ([plan](.plan/L2D6.md), [spec](../../.spec/specs/live2d-authored-playback.md),
  [hardening journal](../../.spec/journal/2026-09-12-live2d-l2d6-4.md)).
- [x] **L2D7 — secondary model behavior** ([plan](.plan/L2D7.md),
  [closeout journal](../../.spec/journal/2026-09-16-live2d-l2d7-closeout.md)).
  Physics, pose, blink/breath/gaze, hit areas, user data, deterministic replay,
  and viewer diagnostics are complete.

## Acceptance gates

### V1

- [x] SDK-on/off configuration and precise SDK version diagnostics.
- [x] Asset and AssetImport remain Live2D-agnostic; import is atomic and
  validates the complete source closure.
- [x] A product creates independent instances while sharing immutable data and
  textures.
- [x] The module emits ordered generic `render::RenderSubmission` data; no
  Live2D semantic branch exists in Render or Graphics.
- [x] Clipped, blended, transparent models render on OpenGL and Vulkan; viewer
  resize, capture, and shutdown evidence is recorded.
- [x] Official R5 image comparison remains an accepted limitation because the
  external sample cannot be built in this environment.

### L2D7

- [x] Product V3 preserves L2D6 data plus optional physics, pose, hit-area, and
  immutable user-data sections, with explicit V1/V2 compatibility.
- [x] Instance-local controllers use one canonical deterministic `AdvanceFrame`
  transaction and reject invalid input without partial mutation.
- [x] User-data queries and current-geometry hit tests are immutable,
  model-local, and SDK-free.
- [x] Viewer tools expose copied behavior diagnostics, fixed-target input,
  follow mode, pause, and step without coupling Runtime to UI APIs.
- [x] OpenGL/Vulkan scripted evidence, SDK-off builds, and L2D7-scoped tests
  are recorded in the [L2D7 closeout journal](../../.spec/journal/2026-09-16-live2d-l2d7-closeout.md).
- [ ] Full repository test suite is green; currently blocked by the unrelated
  `LevelLoaderTest.LoadsCheckedInGameplayLevelFixtures` archive fixture.

## Landed companion: speech bubble overlay

The visual speech bubble is implemented as a presentation component and is not
the semantic emotion system tracked in L2D8. See its [stage plan](.plan/L2D8-speech-bubble.md)
and [implementation journal](../../.spec/journal/2026-09-15-live2d-l2d8.md).

- [ ] Attach the bubble to an authored model point or hit area.
- [ ] Make bubble appearance configurable at runtime.
- [ ] Add a host frame-loop test covering update, mask upload, and shutdown
  ordering.

## Open decisions

- [ ] Approve a redistributable runtime/import test model and record its license
  terms before checking it into the repository.
- [ ] When L2D8 starts, define the semantic emotion vocabulary and the mapping
  from intent to authored expressions, motions, gaze, and optional bubble text.
