# Render Lifecycle

## Startup

`RuntimeContext` supplies `RenderSystemInitInfo` with API selection, native
window, resize dispatcher, bootstrap scene policy, and the async asset-load
queue. `RenderSystem::Initialize()` creates Render-owned policy/services and
initializes the backend through common Graphics contracts.

`PostInitialize()` drains the bootstrap resource work and prepares the logical
renderable source handed to the game-thread Gameplay world. Material/pipeline
resolution stays private to Render.

## Per frame

1. Gameplay publishes value-only source create/update/destroy changes.
2. Render drains and resolves ready source data into `MeshProxy` state.
3. `BeginFrame()` starts RHI frame-local work and applies the scheduled scene
   passes.
4. Scene work writes SceneColor; editor composition is the terminal pass.
5. `EndFrame()` completes the Render frame; the backend owns submission details.

The runtime screenshot command enters through the Game-lane command registry.
Render capture completes after the active SceneColor becomes available; PNG
export completes in Runtime’s screenshot service, then finalizes the command
request.

## Shutdown

Destroy Gameplay before Render so source destruction can reach the sink.
Release screenshot/capture command providers before their services. Render then
releases its resolver-owned static handles before Graphics/backend teardown.
GPU objects are released only after the backend’s submitted work is safe.
