# Live2D Module

The Live2D module is an optional CMake module for integrating the **Live2D
Cubism 5 SDK for Native R5 (`5-r.5`)**. It is enabled by default for engine
development. A local Cubism SDK path is therefore required unless the module
is explicitly disabled.

Current scope includes L2D1 Cubism Core/Framework discovery, lifecycle
ownership, allocator/log bridging, model-instance lifetime tests, and a
placeholder editor preview. Asset import, Cubism-backed rendering, and the
dedicated model viewer are planned in [TODO.md](TODO.md).

## SDK location

Keep the proprietary SDK outside the repository. Set `[SDK_PATH]` to the local
Cubism SDK root, for example:

```text
[SDK_PATH]
```

The CMake finder validates that the root contains:

```text
Core/include/Live2DCubismCore.h
Core/lib/windows/x86_64/143/Live2DCubismCore_MDd.lib
Framework/src
cubism-info.yml
```

The `cubism-info.yml` release must be exactly `5-r.5`. Do not commit Cubism
Core binaries, SDK sample content, or model data without checking the
applicable Live2D license and redistribution terms.

## Enable and build

Run these commands from the repository root in a **Developer PowerShell for
Visual Studio 2022**. `KPENGINE_ENABLE_LIVE2D` is already `ON` by default. Set
the SDK environment variable once for the current configure session:

```powershell
$env:KPENGINE_CUBISM_SDK_ROOT = "[SDK_PATH]"
```

CMake also accepts the legacy `CUBISM_SDK_ROOT` environment variable. An
explicit `-DKPENGINE_CUBISM_SDK_ROOT=...` cache value takes priority over both.
With the environment variable set, configure and build with:

```powershell
cmake -S . -B build-live2d `
  -G "Visual Studio 17 2022" `
  -DKPENGINE_LIVE2D_CRT=MD `
  -DKPENGINE_LIVE2D_MOC_PATH="[SDK_PATH]/Samples/Resources/Wanko/Wanko.moc3"

cmake --build build-live2d --config Debug --target KimPeanutEngine -- /m:2
```

If CMake Tools runs in a newly launched IDE, configure it after setting the
user environment variable or provide the cache entry in the IDE's configure
settings. Do not put the SDK path in a tracked preset or source file.

`KPENGINE_LIVE2D_CRT=MD` selects the SDK's `/MD` static Core library and
matches the engine's default MSVC runtime. Use `MT` only when the whole build
and the matching SDK Core library are intentionally configured for `/MT`.

The MOC path is only a lifecycle/instance test fixture at this stage; it is
not yet an engine asset or rendered model.

## Run the Live2D tests

```powershell
cmake --build build-live2d --config Debug --target Live2DCoreTest -- /m:2
ctest --test-dir build-live2d/engine/test -C Debug -R Live2DCore --output-on-failure
```

The tests verify repeated initialize/shutdown, allocator alignment, shutdown
protection while model leases are alive, and independent parameter state for
two `CubismModel` instances created from the same MOC data.

## Disable Live2D

To create a build tree without the module, explicitly set the option to `OFF`:

```powershell
cmake -S . -B build-live2d-off `
  -G "Visual Studio 17 2022" `
  -DKPENGINE_ENABLE_LIVE2D=OFF

cmake --build build-live2d-off --config Debug --target KimPeanutEngine -- /m:2
```

When disabled, no `Live2D` targets are added and no Cubism SDK path is needed.
When changing an existing build tree, rerun CMake with the desired option so
the cache is refreshed.

## CMake integration

The integration is composed at the module layer:

```text
KPENGINE_ENABLE_LIVE2D=ON
  -> FindLive2DCubism.cmake
  -> Live2DCubismFrameworkRuntime
  -> Live2DRuntime
  -> Live2D -> Live2DModule -> ModuleBootstrap -> Module
  -> Live2DEditor::RegisterEditorExtensions
  -> EditorExtensionRegistry -> EditorUILib
```

### Module ownership and startup

`engine/editor/main.cpp` is the current application composition root. It calls
`kpengine::module::RegisterModules(engine)`, which creates the enabled
`Live2DModule` and passes it to `Engine::RegisterModule`. `Engine` owns the
module after registration and schedules its `OnRegister`, `Initialize`,
`Tick`, and reverse-order `Shutdown` callbacks.

`Live2DModule` owns `Live2DSystem`, and `Live2DSystem` owns the
`CubismLifecycle`. This keeps Cubism startup and teardown inside the Live2D
module. The module bootstrap also asks `Live2DEditor` to register its viewer
factory with the generic `EditorExtensionRegistry`; `EditorUI` does not know
the Live2D type. A future dynamic module loader can replace the application
bootstrap function without changing the engine lifecycle contract.

Relevant files are:

- `cmake/FindLive2DCubism.cmake` — validates the local SDK and creates the
  imported `Live2D::CubismCore` target.
- `engine/module/live2d/CMakeLists.txt` — compiles the selected backend-neutral
  Cubism Framework sources, the Live2D lifecycle runtime, and the editor
  placeholder target.
- `engine/module/live2d/editor/` — Live2D-specific editor UI; currently draws
  an animated aspect-fit placeholder until the Cubism render proxy is ready.
  It registers its workspace component factory through the generic editor
  extension registry; `EditorUILib` does not include Live2D headers.
- `engine/module/module_bootstrap.*` — application composition-root entry that
  creates enabled runtime modules and invokes their editor registration through
  the generic registry without putting feature names in `EditorUI`.
- `engine/editor/ui/editor_extension_registry.*` — generic editor extension
  seam consumed by enabled modules during workspace promotion.
- `engine/module/CMakeLists.txt` — adds the optional `Live2D` aggregate to the
  existing `Module` target only when enabled.
- `engine/test/unit/live2d/` — focused Cubism integration tests.

The Framework renderer backends are intentionally not compiled. OpenGL/Vulkan
rendering will be implemented through KimPeanutEngine's API-neutral Graphics
contract in a later stage. The current editor panel only proves the UI layout
and aspect-fit presentation seam; it does not load or draw a Cubism model.

## Related documentation

- [Architecture and ownership plan](PLANS.md)
- [Roadmap and acceptance criteria](TODO.md)
- [L2D1 stage design](.plan/L2D1.md)
- [L2D1 implementation journal](../../.spec/journal/2026-09-08-live2d-l2d1.md)
