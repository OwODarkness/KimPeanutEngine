# Render Lifecycle

## Startup

Runtime loads the selected level and transactionally prepares its immutable,
selected-API Render asset catalog through Asset and Resource before
`RenderSystem::Initialize()`. `RuntimeContext` then supplies API selection,
native window, resize dispatcher, and that catalog. Render initializes its
policy/services and backend through common Graphics contracts. Initialization
returns a diagnostic result and the lifecycle is explicitly
`Uninitialized → Ready → FrameActive → Ready`, with `ShutDown` terminal.
Partial failures publish no ready RenderSystem state.

The now-empty Render `PostInitialize()` hop is removed. The address-stable
scene coordinator exists before initialization because Runtime
and Gameplay retain its source-sink interfaces. Initialization binds its
material/resource dependencies without replacing those sink objects; rollback
clears state while preserving their addresses.

## Per frame

1. Gameplay publishes value-only source create/update/destroy changes.
2. The Render scene coordinator refreshes materials, drains sources in the
   established order, resolves ready `MeshProxy` state, and produces one
   frame-scoped scene input.
3. `BeginFrame()` starts the backend and matching `FrameContext`, then invokes
   the deferred renderer's fixed sequence.
4. Scene work writes SceneColor; editor composition through the typed borrowed
   presentation bridge remains the optional terminal pass.
5. `EndFrame()` completes the Render frame; the backend owns submission and
   presentation details.

A successful begin establishes the only active bracket and requires an end. If
the backend begins but no valid frame context/recorder can be acquired, R1.5
unwinds the backend bracket immediately and returns to `Ready`; it must not
leave stale active state.

The runtime screenshot command enters through the Game-lane command registry.
Render capture completes after the active SceneColor becomes available; PNG
export completes in Runtime’s screenshot service, then finalizes the command
request.

## Shutdown

Destroy Gameplay before Render so source destruction can reach the stable sink.
Release screenshot/capture command providers before their services. Render then
clears scene/material records, waits for submitted work, and releases frame,
renderer, resolver, and backend state in ownership order. Editor UI and its
borrowed presentation bridge usage end before Render/backend teardown. GPU
objects are released only after submitted work is safe. Shutdown is idempotent
and closes an active frame bracket before cleanup when called at an unexpected
boundary.
