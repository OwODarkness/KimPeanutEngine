# GP7.1 Level Asset — 2026-09-01

## Objective

Implement the [GP7.1 stage plan](../../docs/gameplay/.plan/GP7.1.md): add the
closed V1 CPU level asset format and manager-owned dependency transaction while
leaving Runtime, Gameplay, Render, bootstrap, and live scene behavior unchanged.

## Changes

- Added `KPAT_Level`, `LevelResource`, `LevelPtr`, five closed object record
  variants, optional environment data, typed normalized references, and
  dependency-request metadata under `engine/runtime/asset/`.
- Added `LevelLoader` with strict field, type, finite-value, transform, light,
  camera, cone, and asset-root-relative path validation. It has no
  `AssetManager`, Gameplay, Render, or Graphics dependency.
- Extended `AssetManager::LoadSync` to resolve declared requests after
  `load_mutex_` is released, then register all resolved edges together. Added
  checked `(owner, dependency index, expected type)` lookup and shared
  canonical path-key normalization.
- Added generated-fixture tests for complete records, deduplication,
  normalization/rejection, invalid values, missing dependency failure,
  reverse-edge lifetime, stale lookup, and concurrent same-level loading.

## Reference gate

The GP7 spec's existing Piccolo `Level` and Godot `PackedScene` findings were
used: serialized CPU authoring data stays separate from runtime instantiation,
and centralized asset coordination owns dependency identity/lifetime. No
reference source was copied.

## Validation

- `.\tools\kp.ps1 build AssetUnitTest` — passed after adding the existing
  `Config` interface dependency required by `config/path.h`.
- `AssetUnitTest.exe --gtest_color=no` — 14/14 passed.
- `cmake --build build --config Debug` — passed.
- `ctest --test-dir build -C Debug --output-on-failure` — 197/197 passed.
- Runtime smoke and image capture were intentionally skipped: GP7.1 changes
  no runtime scene or rendering path.

## Remaining risk

GP7.2 must define deterministic model-to-mesh selection and consume the
checked dependency lookup. Bootstrap migration, Actor creation, source
publication, and runtime visual validation remain GP7.2–GP7.5 work.

## Review correction — 2026-09-01

A follow-up code review reopened GP7.1. The implementation boundary is sound,
but completion is blocked by these findings:

1. Resolved dependencies are not revalidated under the final Asset state lock.
   A concurrent unregister can therefore remove a dependency before parent
   registration, producing a level with a stale dependency ID and no reverse
   edge.
2. The level schema represents `lod_bias` as `float` although its existing
   Gameplay and Render consumers use `int`.
3. Some closed-schema and missing-required-string failures do not emit the
   promised path-qualified diagnostic.
4. The named deduplication test contains no repeated reference, so that
   acceptance behavior is not covered.

The existing `AssetUnitTest` executable still passed 14/14 during review. A
fresh `AssetUnitTest` rebuild could not be independently completed because
MSBuild was denied access to
`C:\Users\17519\AppData\Local\Microsoft SDKs` while evaluating the Windows SDK
version (`MSB4184`). This is an environment failure, not a source failure, but
the fixes require a rebuilt focused suite before GP7.1 can be closed again.

## Review resolution — 2026-09-01

The four review findings were resolved:

1. `AssetManager::LoadSync` now revalidates every loader-declared dependency
   under the final `state_mutex_` immediately before parent registration.
2. `LevelStaticMeshRecord::lod_bias` and its parser now use `int`.
3. Closed-field and required-string failures now emit path-qualified loader
   diagnostics, including missing `kind` and camera `projection`.
4. The focused level test repeats mesh references and verifies shared indices
   and unique dependency registration.

Validation after the fixes: rebuilt `AssetUnitTest` passed 14/14; the full
Debug build passed; and the complete CTest suite passed 197/197. GP7.1 is
closed. Runtime smoke and image capture remain deferred because this stage is
Asset-only; runtime level instantiation begins in GP7.2.

## GP7.2 execution — 2026-09-01

Implemented the Runtime-owned static-mesh level instance from
[GP7.2](../../docs/gameplay/.plan/GP7.2.md):

- Added the `RuntimeLevel` target and non-copyable `LevelInstance` with typed
  result errors, Asset-only preflight, explicit model `KPMG_Mesh` resolution,
  validated mesh/material payloads, authored-order Actor creation, stable
  authored-ID lookup, reverse rollback, idempotent unload, and destructor
  cleanup.
- Added `GameplayWorld::ReclaimDestroyedActors()` as the explicit game-thread
  reclamation boundary. `DestroyActor` still invalidates lookup immediately,
  but handle slots are not reusable until owned destroyed storage is reclaimed.
- Added dormant `RuntimeContext::level_instance_` ownership and explicit reset
  before GameplayWorld/Render teardown. Bootstrap scene creation and the V1
  LevelResource schema remain unchanged.
- Added focused RuntimeLevel tests for mapping/order, skipped GP7.3 records,
  preflight rejection, reverse rollback with immediate retry, stale lookup,
  active replacement rejection, idempotent unload, Asset residency, and source
  create/destroy balance. GameplayWorld now independently tests reclamation.

Validation after implementation: `RuntimeLevelTest` passed 6/6, the focused
`GameplayWorldTest` set passed 19/19, the full Debug build passed, and complete
CTest passed 204/204. No Vulkan/OpenGL smoke or image capture was run because
GP7.2 does not enter the live bootstrap path; those checks remain GP7.4/GP7.5
acceptance work.

## GP7.3 execution — 2026-09-01

Implemented [GP7.3](../../docs/gameplay/.plan/GP7.3.md) while keeping the
bootstrap scene and LevelResource V1 schema unchanged:

- Renamed the neutral camera composition to `CreateCameraActor` and updated
  runtime, example, and Gameplay test consumers. Player possession remains a
  separate controller policy.
- Extended `LevelInstance` with a closed `LevelActorFactorySet` for static
  meshes, directional/point/spot lights, and cameras. It preflights every
  authored ID and dependency, creates in authored order, registers the
  optional environment last, and rolls back/unloads in the reverse order.
- Added the Render-owned `EnvironmentSourceDesc`, opaque generational handle,
  single-owner registry, and queued create/destroy contract. Runtime retains
  only the environment handle; the descriptor contains a texture AssetID and
  IBL intensity.
- Added transactional Render environment resolution. A ready texture AssetID
  is resolved without `LoadSync` or authored-path matching, IBL bindings are
  built into a temporary bundle, and the active bootstrap/black fallback is
  retained when resolution fails. Re-registering the same texture reuses the
  retained derived bindings while allowing a different intensity.
- Added focused mixed-level, rollback, environment preflight, source-registry,
  and RenderSystem resolution/reuse coverage. RuntimeContext injects the
  environment sink into its dormant LevelInstance.

Reference gate findings applied: Piccolo informed heterogeneous level
membership and explicit clear/unload; Godot informed separation of camera
creation from active selection and singular world-environment ownership;
Filament informed keeping scene environment/IBL distinct from ordinary lights.
KimPeanut retains its existing typed factories, source queues, and Render GPU
ownership rather than importing reflected object registries or backend types.

Focused validation: the affected `RuntimeLevelTest`,
`EnvironmentSourceRegistryTest`, `GameplayWorldTest`, and
`RenderSystemEnvironmentTest` selection passes 40/40. `cmake --build build
--config Debug` passes, and `ctest --test-dir build -C Debug
--output-on-failure` passes 220/220. `GraphicsSmoke.exe` passes three frames
per API on Vulkan and OpenGL. The rebuilt runtime also exported and visually
verified SceneColor captures through the loopback `capture.screenshot` command:
`save/screenshots/validation/gp7-3-scene-color.png` on Vulkan and
`save/screenshots/validation/gp7-3-scene-color-opengl.png` on OpenGL. The
GP7.3 acceptance criteria are satisfied; bootstrap startup and the V1 schema
remain unchanged, leaving GP7.4 and GP7.5 as the next proposed stages.

## GP7.4 execution — 2026-09-02

Implemented [GP7.4](../../docs/gameplay/.plan/GP7.4.md):

- Extracted shared Asset-root-relative path normalization and used it for
  bootstrap and level references. Bootstrap is now a strict V2 document with
  only `version` and `startup_level`; the old scene and preload-request path
  are removed.
- Completed material dependency closure. Material loading declares typed shader
  program and texture dependencies with deterministic indices, and Render
  resolves those indices without authored path joins or authored `LoadSync`.
  Render-owned default white and flat-normal textures are warmed at startup.
- Moved startup level loading to the game thread before render-thread creation.
  Render reports a typed ready result, Runtime commits or aborts startup through
  a two-phase handshake, and successful startup instantiates the level and
  possesses its shared-rule preferred camera. Startup errors propagate and
  unload partial level state.
- Removed bootstrap Render scene/source transfer, environment path matching,
  bootstrap environment baseline, and hard-coded startup mesh/light/camera
  creation. Added `asset/level/pbr_showcase.level`,
  `point_shadow_validation.level`, and `spot_shadow_validation.level`.

Validation evidence:

- `cmake -S . -B build` completed successfully.
- `cmake --build build --config Debug` completed successfully.
- `ctest --test-dir build -C Debug --output-on-failure` passed 222/222.
- `GraphicsSmoke.exe` passed three frames per API on Vulkan and OpenGL.
- The checked-in PBR startup level loaded and captured successfully through the
  local Runtime command transport on both backends:
  `save/screenshots/validation/gp7-4-pbr-vulkan.png` and
  `save/screenshots/validation/gp7-4-pbr-opengl.png`. Both showed the same
  authored composition and environment after visual inspection.
- Point and spot fixtures were each selected only by changing
  `startup_level`, then restored to PBR. Scene-color, shadow-depth, and
  shadow-visibility capture requests exported successfully on Vulkan/OpenGL:
  `gp7-4-point-vulkan-*`, `gp7-4-point-opengl-*`,
  `gp7-4-spot-vulkan-*`, and `gp7-4-spot-opengl-*` under
  `save/screenshots/validation/`.

## GP7.4 risk resolution — 2026-09-02

Resolved the follow-up review risks without widening ownership boundaries:

- Engine startup now arms a scope guard immediately after render-thread
  creation. Any exception before initialization completes publishes Abort,
  wakes frame/decision waits, and joins the render thread. The entire render
  entry point catches post-ready exceptions and performs idempotent UI/context
  cleanup. Commit is published only after all game-thread startup work,
  including command transport setup, has completed.
- Runtime now has a headless `RuntimeStartupTest` target covering valid commit,
  camera-free, stale/wrong-typed level IDs, controller/possession rejection,
  and terminal `Engine::Clear()` lifecycle behavior.
- Material-owned dependency resolution rejects absolute, drive-qualified, and
  Asset-root-escaping references. Material fixtures now live under the Asset
  root and explicitly cover absolute shader/texture and multi-parent escape
  failures.
- Punctual depth diagnostics are linearized using the scheduled near/far
  ranges. Point and spot fixtures place the light behind the receivers for a
  deterministic caster/receiver silhouette.
- The parent GP7 spec now labels its former bootstrap-scene description as the
  historical entry baseline; GP7.4 is landed and GP7.5 remains the audit.

Focused validation after the fixes: `RuntimeStartupTest` passed 5/5,
material-loader and RuntimeLevel selections passed 31/31, the runtime host
rebuilt successfully, and fresh Vulkan point/spot scene, depth, and visibility
captures exported successfully and showed non-uniform caster/receiver
diagnostics. The bootstrap selection was restored to `pbr_showcase.level`.

## GP7.5 execution — 2026-09-02

Implemented the missing bounded end-to-end lifecycle proof in
`RuntimeLevelTest.FullLifecycleReleasesDependenciesOnlyAfterLevelOwnershipEnds`.
The test loads a synthetic level, instantiates its mesh, verifies that model and
material dependencies cannot unregister while the level owns them, unloads the
instance, unregisters the level, and then releases model/material/mesh assets in
dependency order. No production lifecycle or GPU-inspection API was added.

The final static audit command
`rg -n -S "BootstrapScene|bootstrap_scene|bootstrap_renderable|BuildLoadRequests|PreloadBootstrap|SetBootstrapScene|bootstrap_loaded" engine asset config`
reported no matches in live paths. No Render-side authored file loading,
ownership leak, or backend-native type was found in the serialized/common
gameplay contracts. The already-landed punctual-depth linearization fix was
exercised by the fresh point and spot diagnostic captures.

### Validation

- `cmake -S . -B build -G "Visual Studio 17 2022"` — configure and generate
  passed with Windows SDK `10.0.22621.0`.
- The first non-escalated MSBuild attempt was blocked by the environment:
  `Microsoft.Cpp.WindowsSDK.props(122,5)` could not evaluate the latest SDK
  version because access to `C:\Users\17519\AppData\Local\Microsoft SDKs` was
  denied. The approved escalated build completed successfully; this was an
  environment permission failure, not a source failure.
- `cmake --build build --config Debug --target RuntimeLevelTest RuntimeStartupTest`
  — passed.
- Focused CTest selection for Asset, Bootstrap, Render, RuntimeStartup,
  GameplayWorld, and RuntimeLevel — 91/91 passed.
- `cmake --build build --config Debug` — full Debug build passed.
- `ctest --test-dir build -C Debug --output-on-failure` — 229/229 passed.
- `build/engine/example/graphics/Debug/GraphicsSmoke.exe --graphics-api vulkan`
  and the equivalent OpenGL command — both passed three frames per API.
- `git diff --check` — passed; only existing CRLF/configuration warnings were
  reported by Git.
- Capture integrity was checked with
  `Get-ChildItem save/screenshots/validation -Filter 'gp7-5-*.png' | Sort-Object Name | ForEach-Object { Get-FileHash $_.FullName -Algorithm SHA256 }`;
  all 14 recorded hashes matched.

### Capture evidence

All captures were exported by the rebuilt Runtime host through the local command
registry, inspected visually, and measured at 1904x707. Exact SHA-256 values:

| Capture | SHA-256 |
| --- | --- |
| `gp7-5-pbr-vulkan.png` | `584ebb66230138dfc61667576e558846201c87a63478b486379bac1a1e64b60d` |
| `gp7-5-pbr-opengl.png` | `7a0e65a6e9359f3490a848864b748cdceeb49528a3e3a73fd8a86b315f0007c4` |
| `gp7-5-point-vulkan-scene_color.png` | `9529be4d8d59aa8f8e47944249459e12e442578e4c17087b848f0ab8ea001feb` |
| `gp7-5-point-vulkan-point_shadow_depth.png` | `c26b0ae0587900fa664c080aae94b5b4458725abb767872ddd7f67caa43cf00b` |
| `gp7-5-point-vulkan-point_shadow_visibility.png` | `e0f18deb30b0f40cb12815fb9741a2c9df97808795363d8769abc61c68ad24b2` |
| `gp7-5-point-opengl-scene_color.png` | `b809ff2c6c7c246a4fe0ca882d54cc19e26652df9ff4e822e1f1bcabf232ea76` |
| `gp7-5-point-opengl-point_shadow_depth.png` | `8983df8185a0a86572a3b920f02fcabbcfcb3e533151918d6f43ea33c6fe6e73` |
| `gp7-5-point-opengl-point_shadow_visibility.png` | `4dfc37e49cd257620c686caa287f44e0536f5ee507becfb006f6a5e6f2461c59` |
| `gp7-5-spot-vulkan-scene_color.png` | `9aace43b7ed31ed4e5829829741a1d8f144af73daf1cb6a3be32e7f408e3dda5` |
| `gp7-5-spot-vulkan-spot_shadow_depth.png` | `46eafd0da1f2ec6852efefca66c0e919676878ba7f3a1518b182ef30ea1712d1` |
| `gp7-5-spot-vulkan-spot_shadow_visibility.png` | `cc15fdff5ec2607002d152f3b547deb1d3292d1cd004274b18c889f06b79908f` |
| `gp7-5-spot-opengl-scene_color.png` | `61223c56800111c725799d6547e0bc325697a2c87880a09e3945a5e577d5e597` |
| `gp7-5-spot-opengl-spot_shadow_depth.png` | `2b67c50a982fb2dfd3bb39722a0dd0f45f72ee6b3753750d31049aba158519c2` |
| `gp7-5-spot-opengl-spot_shadow_visibility.png` | `7569e6837705454797bf1d5006895705a500a8e3bb77fc9f6ec33bb294376f2f` |

PBR captures show the authored forest environment, floor, rock, bunny, gold
sphere, teapot, and Cerberus composition on both backends. Point and spot scene
captures show their isolated caster/receiver fixtures. Depth captures are
non-uniform linearized punctual-depth views with expected white far-range areas;
visibility captures contain non-uniform dark occlusion regions. The Vulkan and
OpenGL results are semantically equivalent; byte-identical PNGs were not a
requirement.

### Diagnostics, skipped checks, and remaining risk

No validation or debug errors surfaced in the successful smoke/runtime command
responses. The smoke logs reported the NVIDIA Vulkan device and OpenGL bindless
path as ready. A separate persistent external validation-layer log was not
captured, and no ASan/TSan run was performed. Command-line startup-level
override, live level replacement/streaming, multiple active levels, editor
level authoring, asynchronous load cancellation, and new point-shadow
cubemap/subresource contracts remain explicitly deferred beyond GP7.

`config/bootstrap.json` was restored to:

```json
{
    "version": 2,
    "startup_level": "level/pbr_showcase.level"
}
```

## Post-GP7 startup-level override — 2026-09-02

Implemented the selectable, launch-scoped startup-level option from
`docs/gameplay/.plan/STARTUP_LEVEL_OVERRIDE.md`.

- `RuntimeLaunchOptions` now owns typed parsing for `--graphics-api`,
  `--agent-port`, and `--startup-level`. Unknown, duplicate, missing, malformed,
  unsafe, wrong-type, and wrong-namespace values fail before Engine construction.
- `Engine` validates and stores one immutable pre-initialize override, always
  validates Bootstrap V2, chooses CLI over Bootstrap, logs the source/path, and
  passes only the resulting Level AssetID into RuntimeContext.
- The option is launch-scoped; no live level-switching command or transition
  policy was added. `config/bootstrap.json` remains unchanged.
- Agent instructions now show the combined launch and `capture.screenshot`
  workflow in [AGENTS.md](../../AGENTS.md) and
  [agent_transport.md](../../docs/command/agent_transport.md).

Direct rebuilt-runtime proof passed for all three checked-in levels on both
backends using `--startup-level` plus `--agent-port`: PBR Vulkan/OpenGL, point
shadow Vulkan/OpenGL, and spot shadow Vulkan/OpenGL. The agent transport queued
and exported SceneColor captures for each selected fixture. The earlier
non-escalated MSBuild Windows SDK access error recurred; the approved escalated
build passed and remains an environment permission issue, not a source failure.
