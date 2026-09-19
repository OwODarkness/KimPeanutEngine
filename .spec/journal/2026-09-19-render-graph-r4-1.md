# Render Graph R4.1 journal — 2026-09-19

## Status

R4.1 graph correctness prerequisites are implemented and validated.

## Changes

- Preserving `None`/`Load` writes now depend on the producer of the previous
  logical version. `Clear` writes explicitly discard previous contents.
- `DeferredRenderer` constructs a frame-local table for every active graph
  texture lifetime after transient acquisition. Pass attachment lookup,
  sampling lookup, and transition execution use exact graph handles; the
  `RenderPassResource` ordinal resolver was removed from execution.
- Required texture transitions now propagate backend failure. Buffer transition
  intents are dispatched through `CommandRecorder::RequireBufferUsage`.
  Vulkan emits a synchronization2 buffer barrier; OpenGL accepts the portable
  requirement through its implicit ordering model.
- Unresolved bindings fail required recording and reach the deferred frame
  result/graph-frame failure state while preserving the existing recoverable
  `RenderSystem` frame bracket. An external terminal transition failure causes
  finalization to report failure.

## Corrections during implementation

The first build caught const-qualified access from the new named binding
resolver. The resolver was made non-const because it returns mutable
frame-target ownership. No behavioral workaround or ordinal fallback was
introduced.

## Validation

- Sanitized-environment Debug build targets passed:
  `RenderGraphTest`, `RenderSystemTest`, `GraphicsContractTest`, and
  `GraphicsSmoke`.
- Focused CTest regex
  `RenderGraph|RenderSubmission|Graphics`: 42/42 passed.
- Expanded focused CTest regex
  `RenderGraph|RenderSubmission|RenderSystem|Graphics`: 57/57 passed.
- Full Debug build passed with `cmake --build build --config Debug`.
- `GraphicsSmoke.exe`: passed six frames per API. Vulkan emitted no validation
  errors. OpenGL retained its existing intentional invalid-geometry rejection
  diagnostics, and fallback capture completed.
- The six smoke captures retained their R4.0 SHA-256 values:
  - Vulkan final: `7A26D2994861341DFE2F6B81BB99682BFA1F1C4167AC7B92C537DE4122B10F43`
  - OpenGL final: `7AE86480265FDFE1F31F8D1227F3D59A48F9CB58DC245F60C94F3E298BBE386C`
  - Vulkan D2: `828F9F602411A120183AC8EE5FCCCDAC88DE57E139FB6A28527CBAA6C1A1422C`
  - OpenGL D2: `1FF1F3EFB148BE1423C310F69F98AF5F5FAE1B019B4764A41F11833112DE6349`
  - Vulkan D5: `D32FEA4132A221477BA677D238365E312C75B976B989490F1E144551A9046D7`
  - OpenGL D5: `61BBD0EFAAD3FEBA1493477A0A659D5E52A8575CADC6DACA3096787E3A6F8D1E`
- Runtime log: `save/logs/r4-1-smoke.txt`.

The full suite completed at 962/963 tests. The sole failure was the known
unrelated `LevelLoaderTest.LoadsCheckedInGameplayLevelFixtures`, which still
references the absent archived model `model/rock1-bl/rock2` from
`pbr_showcase.level`.
