# SET2 — Rendering settings integration

**Status: implemented; visible Vulkan/OpenGL and focused headless acceptance passed.**
Parent: [roadmap](../TODO.md). Implementation evidence is recorded in the
[runtime settings journal](../../../.spec/journal/2026-11-10-runtime-settings.md).

## Why Render is first

The existing typed settings and mutex-protected `RequestPathTraceSettings`,
`RequestScreenSpaceAoSettings`, `RequestTemporalAaSettings` and
`RequestHeightFogSettings` already provide a domain boundary. The missing pieces
are startup resolution, centrally registered metadata, applied-state publication
and acknowledgement. Reuse those operations rather than move Render logic into
SettingsRegistry.

## Initial sections

| Section/property | Typed owner and application |
|---|---|
| `render.startup.ray_tracing_support` | RenderStartupSettings; requested graphics feature support; restart required |
| Persistent `render.path_tracing` fields | PathTraceSettings; typed complete candidate and next safe frame; transient output probes remain commands |
| `render.screen_space_ao` | ScreenSpaceAoSettings; next safe frame |
| `render.temporal_aa` | TemporalAaSettings; next safe frame |
| `render.height_fog` | HeightFogSettings; next safe frame |

The schema exposes the persistent PathTraceSettings fields as metadata rather
than replacing the typed struct. Conversion validates complete candidates and
keeps transient output probes outside persistent state. Fixed and adaptive
sample properties retain their mode-specific meanings.

## Placement and startup migration

1. Add the RenderSettings satellite target and provider files named in
   [architecture](../PLANS.md). Pure schema contribution needs no RenderSystem.
2. Add typed RenderStartupSettings without copying window/backend callbacks
   from RenderSystemInitInfo into preferences. Graphics still owns GPU feature
   negotiation and reports capability; Render owns the requested policy.
3. Initialize the Runtime-owned settings service before `InitializePresentation`
   and any host initialization that can create a device. Built-in Render schema
   contribution is explicit; optional modules contribute through ModuleBootstrap
   without Runtime depending on their implementation.
4. Change the entry-point/composition path to translate only explicit
   `--disable-ray-tracing` / `--disable-path-tracing` flags into session overrides.
   Preserve that disabling RT also disables PT for this launch, and retain the
   parser's duplicate-option diagnostics. Add presence tracking to its public
   parse result: current Boolean defaults must not mask disk values.
5. Resolve startup values into RenderSystemInitInfo and initial PathTraceSettings.
   Remove `Engine::SetRayTracingEnabled`, `Engine::SetPathTracingEnabled`, and
   duplicate forwarding flags in RuntimeContext when all callers have migrated.
   Keep compatibility at CLI/command boundaries rather than preserve competing
   Engine configuration paths. VSync and startup-level ownership are SET5.

The current implementation gates `path_tracing_enabled_` at initialization and
passes it into DeferredRenderer. Audit the downstream resource/shader setup
before claiming that starting with path tracing off supports later enabling.
If resources are unavailable, return RestartRequired with a reason rather than
advertise an unimplemented live resource rebuild. Preserve existing API fallback.

## Live changes and confirmation

The provider converts a validated complete section into a typed request while
preserving current fields outside its schema. Extend Render's existing queue
with request serial/completion publication. A Boolean queue result alone cannot
prove application. Acknowledge after the safe-boundary setter has run and its
active values are known; report rejected, superseded and shutdown-cancelled
requests distinctly. Do not wait synchronously for a frame on the UI thread.

Publish a copied settings/capability snapshot from the render owner, analogous
to GetPublishedMetrics. Keep resource creation, accumulation invalidation,
profiling-window reset and GPU retirement inside existing Render/Graphics owners.
Do not reset history merely because a catalog revision or UI draft changed.

Move existing Path Tracing, SSAO, TAA and Height Fog preference commands through
this adapter and preserve command names/argument validation. Concurrent command
and UI edits use the same revision checks. Internal legacy requests publish
active values and cannot silently overwrite another request without a terminal
status. Diagnostics and probes remain transient commands. A host without a
scene renderer reports live edits as unavailable and does not create Scene3D to
service them.

## Validation and acceptance

- Unit/contract: stored startup flags arrive before backend construction;
  omitted CLI flags preserve disk values; explicit flags win; invalid CLI fails;
  no preference-specific Engine setters or RuntimeContext duplicate flags remain.
- Render contracts: accepted versus applied serials, queued edits before scene
  promotion, invalid values, supersession, capability rejection, restart intent,
  partial startup failure, frame skip/failure and cancellation on shutdown.
- Restart: saved startup support changes affect the next constructed backend,
  while the current device and active values remain unchanged.
- Integration: existing command names operate through the shared path; an
  unexposed field changed by another command survives an exposed-property edit.
- Visible Debug Vulkan validation-enabled and OpenGL checks: canonical textured
  `level/sponza.level`, settled residency, base_color and scene_color captures,
  supported PT/hybrid toggles, restart-only change, loading, resize and close.
  Check actual active RT mode through Runtime stats, not a checkbox alone.
- Standalone host smoke: scene settings do not initialize scene services in an
  owned-window viewer. Preserve host-configuration tests and normal startup.

Build SettingsUnitTest, RuntimeLaunchOptionsTest, RuntimeStartupTest,
RenderSettingsTest, RenderSystemTest, RenderPassScheduleTest,
GraphicsContractTest, GraphicsSmoke and KimPeanutEngine as affected; run their
discovered CTest cases/labels. Shared startup/CMake changes trigger broad
validation under the repository matrix.
Never run concurrent builds. Engine/smoke launches use the approved path outside
the sandbox; engine captures stay under `save/screenshots/validation/*.png` and
use the Runtime registry plus poll. GUI settings-window acceptance belongs to
SET3. No performance claim is needed for this configuration migration.
