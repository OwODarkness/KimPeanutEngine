# Stable Scene3D texture startup — 2026-10-13

## Objective and scope

The user reported visible startup instability in canonical `sponza.level` as
progressive path tracing repeatedly reset with `materials_changed`. They prefer
waiting on the loading screen to presenting a scene whose texture detail is
still changing. The implementation changes Runtime's initial scene-loading
policy; Asset, Render, and Graphics retain their existing ownership and APIs.

Starting revision: `699ac30`, with extensive existing uncommitted R7.3/TAA
changes. Those edits were preserved. This task changes only
`engine/runtime/engine.cpp` and this journal.

## Diagnosis

`Engine::Initialize` configured six tail mip levels before loading the startup
level. Native texture loading published that coarse view and deferred the full
chain. `RenderResourceResolver` started the full-resolution read when resolving
material bindings, then `PollTextureResidency` requested refreshed bindings as
each higher-resolution payload arrived. GPU texture identity participates in
the ray-tracing material signature, so path tracing correctly discarded the
previous estimator's accumulated samples after a binding changed.

The `R4.6 path tracing resource preparation completed` message reports
`PathTracingPass::PrepareResources` and frame-target preparation; it is not a
scene-wide texture-residency barrier. The earlier suggestion of missing
placeholder textures is superseded by the actual coarse-mip promotion path.

The user's original endpoint `127.0.0.1:37403` refused a connection during this
investigation; no live observation of that original process is claimed.

## Change and invariants

- Scene3D startup selects zero initial-mip-limit, which the existing native
  loader interprets as a complete mip chain. Loading/verification occurs before
  scene startup publication.
- Host-presentation modes retain the six-mip policy. The six-mip setting is
  restored after initial level loading so later loads retain streaming behavior.
- History invalidation remains intact for genuine material edits, camera
  movement, and later streaming changes. Path tracing remains enabled when the
  startup scene is revealed.
- No public type, shader, RHI, dependency direction, GPU owner, or resource
  retirement rule changes. This is a complete-startup policy, not a new
  view-dependent texture streamer.

## Validation

```powershell
cmake --build build --config Debug --target KimPeanutEngine RuntimeStartupTest
ctest --test-dir build -C Debug -R 'RuntimeStartupTest|RuntimeStartupCoordinatorTest|RuntimeFramePolicyTest' --output-on-failure
```

Both targets built; the 14 focused runtime/startup tests passed. No additional
constant-mirroring unit test was added: the visible startup check exercises the
policy's actual texture/residency/history behavior.

The rebuilt Debug Vulkan application was launched outside the sandbox:

```powershell
build/Debug/KimPeanutEngine.exe --graphics-api vulkan --startup-level level/sponza.level --agent-port 37413
```

Normal Debug validation remained enabled. Its `GLFW30` window was verified
visible and foreground on the `Default` desktop. Fixture conditions were the
canonical camera at `[10.80743, 1.59222, 0]`, authored materials/lights, and
1094×631 viewport. TAA was ineffective while PT was active.

An observer connected as the transport opened and recorded 80 snapshots over
27.235 seconds, including capture export overhead. All snapshots reported
complete tracked texture residency. Three initial snapshots precede active PT;
the remaining 77 have active PT. Sample counts increased from 0 to 1,135 with
no decreases. Reset reasons were only `history_uninitialized` and `none`, never
`materials_changed`. Early and settled images retain the same geometry and
texture detail while Monte Carlo noise converges. Initial history setup is
still allowed; full loading does not remove normal path-tracing noise.

Evidence:

- `save/diagnostics/sponza-stable-startup-20261013/runtime-samples.json`
- `save/screenshots/validation/sponza-stable-startup-early-20261013.png`
- `save/screenshots/validation/sponza-stable-startup-settled-20261013.png`
- `save/logs/sponza-stable-startup-debug-build-20261013.log`
- `save/logs/sponza-stable-startup-20261013.stdout.log` / `.stderr.log`

Captures used Runtime `capture.screenshot` and request polling. The engine
remains visible for the user. The Asset phase reported 33.863 seconds in Debug;
this is a diagnostic observation, not a performance result or a matched
before/after comparison. The additional startup wait is an explicit tradeoff.

## Industry context and future limits

Documentation was checked using the engine-reference workflow; no external
source implementation was copied and no new streaming roadmap was adopted.

- [Unreal texture streaming overview](https://dev.epicgames.com/documentation/unreal-engine/texture-streaming-overview-for-unreal-engine)
  (UE 5.8 docs) describes view/texel-density-based desired mip calculation,
  memory-pool limits, priorities, and asynchronous streaming requests.
- [Unreal texture streaming configuration](https://dev.epicgames.com/documentation/en-us/unreal-engine/texture-streaming-configuration-in-unreal-engine)
  exposes a separate fully-load-used-textures policy. Full resolution is an
  available policy, rather than the only industry loading strategy.
- [Unity preloading](https://docs.unity.com/en-us/engine/7000.0/manual/materials-and-shaders/textures/texture-loading/texture-streaming/preload)
  explicitly preloads camera mip requirements before enabling the camera to
  avoid texture pop-in. [Unity's loaded-mip query](https://docs.unity.com/en-us/engine/6000.0/script-reference/unityengine/texture2d/isrequestedmipmaplevelloaded)
  distinguishes requested detail from completed loading.

For a larger engine, a future target is required-detail prewarming under a
memory budget before scene activation. Path tracing must also account for
textures used by secondary rays. Runtime would own the scene-readiness policy,
Asset CPU reads, Render required-detail decisions, and Graphics uploads/fences.
This remains discussion, not implemented functionality. Current initial scene
loading deliberately takes the simpler full-chain route.

OpenGL, host-presentation runtime modes, and performance/VRAM limits were not
retested; their startup policy is unchanged. A full suite is not required for
this private Runtime implementation change and was not rerun. Visual evidence
is scoped to the rebuilt Debug Vulkan Sponza startup. Unrelated uncommitted
TAA work is not accepted or validated by this repair.
