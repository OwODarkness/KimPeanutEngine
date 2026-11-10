# Host-owned application configuration

## Operation and placement

Operation and acceptance: construct each host with typed startup configuration
before Engine initialization; the host owns those settings and Engine owns the
resulting `IApplicationHost`.

Existing candidates inspected: `Engine` setters/getters and state in
`engine/runtime/engine.h/.cpp`, CLI input in `engine/runtime/launch_options.*`,
host construction in `engine/runtime/host/application_host.*`, configuration
reads in `Live2DViewerHost` and `PanelViewerHost`, and the entry point in
`engine/editor/main.cpp`.

Placement and reason: per-host values live in typed Live2D and Panel host
configuration; shared Panel content lives under Panel's runtime contract and
is consumed by Live2D's bubble. Standalone capture/resize values use a neutral
Runtime host configuration type, but their lifetime and interpretation belong
to the configured host. The registry is reused as application composition;
Engine no longer constructs hosts from mode IDs during initialization.

Consumers, ownership/lifetime, and validation: each host stores its copied
configuration from construction through shutdown. RuntimeLaunchOptions remains
the parser/composition input. Scene3D's startup-level selection remains on
Engine because its synchronous Asset preparation and promotion sequence is a
separate ownership move.

## Reference study

The engine-reference index points to Sakura's `engine` branch and describes its
Live2D Viewer as a standalone application. GitHub's public repository page was
read, but the source tree was blocked by the available browser, so no concrete
factory or configuration source was verified. The implementation therefore
uses the existing KimPeanutEngine host registry and avoids claiming a copied
Sakura pattern.

## Changes

- Added typed `Live2DViewerConfiguration` and `PanelViewerConfiguration`.
- Added `PanelContentConfiguration` for text/glyph input shared by the panel
  display and Live2D bubble.
- Added `StandaloneViewerLaunchConfiguration` for capture, resize, view,
  alpha-clear, and exit behavior.
- Host factories now receive `RuntimeLaunchOptions` and construct configured
  hosts. The entry point composes and injects the selected host before
  `Engine::Initialize()`.
- Removed Live2D, Panel, standalone capture, and resize state and APIs from
  Engine; the startup-level path remains unchanged.

## Validation

- `cmake --build build --config Debug --target KimPeanutEngine` — passed.
- Unit and runtime suites were not run. Visible standalone-host smoke remains
  pending.
- `tools/agent/placement.ps1` — passed structural checks; semantic ownership was
  reviewed against the existing host and Panel boundaries.
- `git diff --check` — passed.
