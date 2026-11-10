# Runtime settings planning evidence

- Status: SET1 in progress; schema, store, and layer-resolution slices implemented
- Date: 2026-11-10
- Baseline: `796ced5`; working tree clean before documentation edits
- Spec: [runtime-settings](../specs/runtime-settings.md)
- Parent TODO: [Settings roadmap](../../docs/settings/TODO.md)

## Operation and placement

Operation and acceptance: plan centrally registered settings and domain pages;
explain why, ownership, source placement, initialization, safe application,
persistence, rollout and validation without implementing runtime behavior.

Candidates inspected: Engine/RuntimeContext startup flags and initialization;
RenderSystemInitInfo and existing typed request methods; Reflection values,
metadata and lifecycle; CommandRegistry lifetime tokens/game-lane dispatch;
Actor Editor snapshots/drafts; Editor settings and UI composition; ModuleBootstrap
contributors; current typed host configuration; relevant docs and CMake targets.

Placement and reason: propose an independent RuntimeSettings target for schema,
layers, routing and persistence. Keep schemas/conversion with their domain
adapters and active state/application with domain owners. Reuse Reflection's
neutral vocabulary, Render's existing request boundary and Editor snapshot/edit
pattern. Keep Engine orchestration thin and preserve transient host inputs.
The registry and adapters have current consumers in startup, commands and the
Rendering page; no generic utility bucket or new thread framework is proposed.

Consumers and lifetime: startup resolves typed values before device creation;
Editor/commands consume copies; provider tokens detach before owners die.
The plan requires acknowledgement at the domain boundary, cancellation on
shutdown and rejection of late/stale results. Documentation changes introduce
no actual dependencies, resources or runtime lifetime changes.

## Reference evidence

The preceding design discussion inspected Epic's 5.8 DeveloperSettings and
ISettingsModule registration API documentation and Godot `master` source
`core/config/project_settings.h`, file blob SHA
`1e5c6f1138b434979bb65df04bbd81600184cfec`. The Godot rendering-server file lookup
did not resolve; no claims about its rendering apply path are made. Unreal
source was not accessed; its findings are limited to documented API contracts.
Links and adopt/reject decisions are recorded in the architecture document.

## What changed

- Added Settings architecture, local guide, acceptance roadmap and concrete
  headless/Rendering stage designs; added the proposed multi-stage execution spec.
- Linked planning from Engine, Editor, reference index and project status.
- No C++, build configuration, preference files or runtime behavior changed.

## Validation

Required level: Level 0 (documentation only). Placement evidence ran before
editing against existing Engine, startup, Render, Reflection and Editor settings
paths and again during final review; structural checks passed both times.

- `tools/agent/placement.ps1 -Paths engine/runtime/engine.h,engine/runtime/runtime_startup.h,engine/runtime/render/render_system.h,engine/runtime/reflection/reflection_types.h,engine/editor/settings -Pattern 'RequestPathTraceSettings|ReflectionPropertyMetadata|SettingsRegistry|ISettingsProvider'`
  — PASS; reused owners and proposed satellite adapter placement were also
  reviewed semantically. No actual source placement/dependency changes occurred.
- Inline Python local-link/anchor scan over modified/untracked Markdown,
  comparing links with HEAD — PASS for all 39 introduced links; 486 local links
  checked across 12 files. Two pre-existing `docs/status.md` anchors targeting
  AP1.1/AP1.2 in `2026-09-09-asset-ap1-0.md` were flagged and left unchanged.
- `git -c core.safecrlf=false diff --check` — PASS. The per-command Git option
  suppresses line-ending conversion warnings without changing repository config.
- Source review confirmed the actual CLI flags are `--disable-ray-tracing` and
  `--disable-path-tracing`; SET2 records their existing RT-implies-PT disable
  behavior and the need to expose explicit-option presence in the parse result.

Planning-stage validation above covered the documentation-only deliverable.

## SET1 schema registration slice

- Added Reflection-backed setting value/schema contracts and a headless
  `SettingsRegistry` that rejects malformed definitions and duplicate section
  keys, returns copied schemas, and seals registration.
- Registration checks canonical IDs, types, finite/ranged defaults, metadata,
  enum options, allowed scopes, and domain validation against copied defaults.
- Added `RuntimeSettings` as a separate target depending on Reflection only;
  it is not wired into Runtime composition. Added focused tests for the
  registration contract.
- Reused Reflection's `ReflectionValue` and metadata vocabulary. Kept schema
  validation local to RuntimeSettings because Reflection's analogous validator
  is private and tied to live reflected-object descriptors. No new runtime
  dependency or subsystem lifetime was introduced.
- Placement evidence before edits: `tools/agent/placement.ps1` over Runtime,
  Reflection and unit-test owners; it confirmed Reflection as the value and
  metadata source and no existing central settings registry.
- Validation: CMake configure PASS; `SettingsUnitTest` Debug build PASS;
  `ctest --test-dir build -C Debug -L settings --output-on-failure` PASS (7/7).
  Runtime/GUI checks were not applicable because the service is not composed.

## SET1 settings store slice

- Added a per-file `SettingsStore` for project or user scope. It loads versioned
  JSON overrides against a sealed schema catalog and returns copied overrides
  plus diagnostics. JSON remains a private implementation dependency.
- Unknown sections and unknown properties survive saves. Known values are
  checked against complete defaults plus their overrides; malformed known
  sections and unsupported file/schema versions fall back without authorizing
  a rewrite. Domain migration callbacks receive copied scalar values and may
  identify legacy keys to remove on the next explicit save.
- Save accepts only caller-supplied accepted overrides, checks scope and disk
  content conflicts, writes and closes a same-directory temporary file, then
  atomically replaces the destination. Project and user files use the same
  format and distinct scope checks.
- Reused `SettingsRegistry::ValidateValues` for schema and domain validation;
  kept path, JSON document, unknown fields and disk baseline inside the store.
  RuntimeSettings adds only a private nlohmann/json dependency. No file is
  selected or written by Runtime composition yet.
- Placement evidence before and after edits: RuntimeSettings, Reflection, the
  existing content metadata/audio atomic-write owners, and settings tests were
  reviewed; structural checks passed. JSON support follows the existing
  `KP::NlohmannJson` target and Windows replacement follows the existing
  same-directory publication pattern, without deleting the destination first.
- Validation: CMake configure PASS; Debug `SettingsUnitTest` build PASS;
  settings CTest PASS (15/15), including explicit save/reopen, unknown data,
  malformed/future input preservation, migration, invalid section fallback,
  scope/schema validation and changed-on-disk conflict. Tests use isolated
  temporary directories under the build tree.

## SET1 layer resolution and provenance slice

- Extended `SettingsRegistry` to own project, user and session override maps.
  Resolution starts from compiled defaults, then applies project, user and
  session values in precedence order and publishes copied per-property source
  metadata and a value revision.
- Persistent scopes are checked against each section schema. Removing a
  property override reveals the next lower layer. Each layer candidate is
  validated as a whole against the complete lower-layer candidate; invalid
  layers publish a diagnostic and leave the previous effective section intact.
- Callbacks run after the registry copies schemas/layers out from under its
  mutex. Registry reuse remains the owner for schema and resolved configuration;
  no additional service or dependency was introduced.
- Validation: Debug `SettingsUnitTest` build PASS; settings CTest PASS (19/19).
  Added precedence/provenance, remove-override/default restoration, invalid
  whole-layer fallback, and persistent-scope versus session tests.

## SET1 provider and edit lifecycle slice

- Added the optional `ISettingsProvider` apply contract, a move-only registration
  token, and one-shot completion handles that remain safe after unregister or
  registry shutdown. Providers receive copied full candidates and changed
  values; the registry invokes them only through an explicit owner-lane pump,
  outside its mutex.
- Edits validate complete candidates against a section revision, reject stale
  drafts, and allow one in-flight request per section. Provider acknowledgement
  distinguishes applied, rejected and superseded outcomes; only applied values
  become accepted session intent. Restart-required edits update session intent
  without a live provider.
- Provider release cancels unresolved requests when the registry next pumps or
  reads their result. Completion handles reject duplicate, stale-generation,
  post-unregister and post-shutdown acknowledgements. Request and retained
  completion capacities are bounded; full capacity returns Busy.
- Registry reuses schema validation and owns request IDs, generations and
  section revisions. Providers remain non-owning domain objects; registration
  tokens invalidate their binding, while composition must finish owner-lane
  dispatch before destroying the provider.
- Validation: Debug `SettingsUnitTest` build PASS; settings CTest PASS (27/27).
  Tests cover owner-lane dispatch, apply/reject, invalid provider output, stale
  drafts, Busy, unregister/cancellation, late acknowledgements, shutdown,
  restart-only edits, queue bounds and completion-history bounds.

## Remaining work and risks

SET1 remains in progress; store write-failure injection and complete runtime
composition acceptance remain open. The provider bridge is not yet connected
to Render, whose existing request methods still report queue acceptance rather
than frame-boundary application. SET2 must add that owner-side acknowledgement
and startup audit. SET2–SET5 acceptance and later-domain file migrations remain
open; continue with SET1 acceptance before integrating Rendering.
