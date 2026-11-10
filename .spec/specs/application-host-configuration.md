# Application host configuration ownership

- Status: active
- Owner: Runtime application composition
- Parent TODO: [MODE1.7](../../docs/engine/TODO.md)

## Objective

Move application-specific startup settings out of `Engine` and into typed host
configuration constructed during application composition. Preserve the
separate Engine-owned Scene3D startup-level path for a later change.

## Current state

`Engine` stores Live2D model selection, shared Panel content, Panel appearance,
and standalone viewer capture/resize settings. Live2D and Panel hosts read these
values through Engine getters. The host registry constructs hosts during
`Engine::Initialize()`, after the entry point has assigned those values through
Engine setters.

## Scope and non-goals

- Move Live2D model and bubble content into `Live2DViewerConfiguration`.
- Move Panel content and appearance into `PanelViewerConfiguration`.
- Move standalone capture and resize values into host-owned configuration.
- Construct configured hosts from `RuntimeLaunchOptions` before Engine
  initialization, then inject Engine's owned `IApplicationHost`.
- Keep `startup_level_override_` and Scene3D asset-loading sequencing unchanged.
- Do not redesign Runtime's general host lifecycle or remove application mode
  metadata still used by host validation and module registration.

## Invariants

- Each host owns its typed configuration for its full lifetime.
- Runtime launch options remain parser/composition input, not Engine state.
- Panel content is reusable by the Panel viewer and the Live2D bubble.
- Engine still owns exactly one selected host and drives initialization,
  ticking, and teardown.
- Host factories do not require an Engine instance to construct a host.
- Scene3D startup level selection remains on the existing Engine path.

## Stages

1. Add typed host configurations and a shared standalone launch configuration.
2. Change host factories to accept launch options and construct typed hosts.
3. Compose and inject the selected host in the application entry point.
4. Remove Live2D, Panel, capture, and resize setters/getters/state from Engine.
5. Update design, roadmap, and completion evidence.

## Acceptance criteria

- [x] Live2D and Panel startup settings are owned and consumed by their hosts.
- [x] The entry point composes a configured host before Engine initialization.
- [x] Engine's public API no longer exposes Live2D, Panel, capture, or resize
  settings.
- [x] Engine's startup-level ownership remains unchanged.
- [ ] Debug runtime startup/capture smoke passes for both standalone hosts.

## Validation plan

Level 1: build `KimPeanutEngine` in Debug. Level 3: validate panel-viewer and
Live2D viewer startup/capture/shutdown through the supported visible runtime
path. Do not use sandboxed engine launches.

## Risks and open questions

- Runtime visual/startup validation is pending after the host factory API move.
- Sakura's public README documents its standalone Live2D Viewer, but the source
  tree could not be inspected through the available source browser; no source
  pattern was adopted.
- Scene3D startup-level loading remains coupled to Engine and is explicitly
  deferred.
