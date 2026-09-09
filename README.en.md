# KimPeanut Engine

<p align="center">
  <img src="./config/icon.png" alt="KimPeanut Engine icon" width="96" height="96">
</p>

![KimPeanut Engine](./docs/images/main.png)




KimPeanut Engine is a C++ game-engine R&D project focused on rendering and
low-level infrastructure. It emphasizes explicit resource ownership, GPU
lifetime management, testable module boundaries, and portable Vulkan/OpenGL
graphics contracts.

> Chinese documentation: [README.md](README.md)

## Contents

- [About](#about)
- [Features](#features)
- [Technology](#technology)
- [Third-party](#third-party)
- [Build](#build)
- [Test](#test)
- [Documentation](#documentation)
- [Contributing](#contributing)
- [License](#license)

## About

KimPeanut Engine is a C++ engine research project for real-time rendering. Its
main data flow is explicit: assets enter through import and cook, Resource turns
them into runtime products, Render decides what to draw, and an API-neutral RHI
submits work to Vulkan or OpenGL. The project prioritizes clear ownership, GPU
lifetime, and testable cross-backend boundaries for editor, tooling, and gameplay
prototypes.

See the [architecture overview](docs/architecture_overview.md) for module
relationships, data flow, and ownership boundaries.

## Features

The main path is `Asset → Resource → Render → RHI`.

### Core capabilities

- **Asset**: `AssetManager` owns typed asset IDs, path deduplication, generation checks, caching, dependencies, and CPU-side lifetime; synchronous and asynchronous loads share one pipeline.
- **Resource / Import / Cook**: Converts CPU assets into native products and render data; imports and cooks models, materials, and textures with dependency closure, image decoding, mipmaps, format policy, and content-addressed output.
- **Render**: Owns RenderWorld, MeshProxy, materials, render targets, fixed pass scheduling, SceneColor, shadows, and debug capture; it does not read arbitrary source files.
- **Graphics / RHI**: Connects Vulkan and OpenGL through API-neutral handles, descriptions, and `RenderBackend`, owning GPU resources, pipelines, commands, synchronization, and deferred release.

### Supporting systems

- **Runtime**: Provides windowing, input, scripting, gameplay, and core runtime services.
- **Command**: Runtime owns one `CommandRegistry`; the editor console, Lua, tests, and local automation share typed commands with game-thread queuing, structured results, and controlled local JSON-lines transport.
- **Audio / TTS**: AudioSystem handles playback; TTS connects to GPT-SoVITS through a provider interface with synchronous/asynchronous and buffered/streaming synthesis.
- **Editor / optional modules**: Dear ImGui editor support and an optional Live2D import, cook, and runtime product module.
- **Validation**: Unit or contract coverage for Asset, Graphics, Render, Audio, Script, Gameplay, and Command.

## Technology

| Area | Choice |
| --- | --- |
| Language standard | C++17 |
| Build system | CMake |
| Toolchain/platform | MSVC / Visual Studio 2022; Windows-first validation |
| Graphics backends | Vulkan and OpenGL |
| Window system | GLFW |
| Editor UI | Dear ImGui |
| Model import | Assimp |
| Image/audio | stb_image and miniaudio |
| Scripting | Lua / sol2 |
| Testing | GoogleTest and CTest |

## Third-party

Third-party libraries are kept separate from engine code. The main dependencies are:

| Use | Main dependencies |
| --- | --- |
| Asset / Resource | Assimp, stb_image, nlohmann/json, SQLite |
| Render / RHI | Vulkan, shaderc, GLFW, glad, Dear ImGui |
| Audio / TTS | miniaudio, cpp-httplib |
| Script / Reflection | Lua, sol2, EnTT, magic_enum |
| Tests | GoogleTest |

See the [third-party dependency inventory](third_party/README.md) for versions, licenses, CMake targets, and integration details.

## Build

Requirements:

- Visual Studio 2022 with the C++ workload
- CMake
- Vulkan SDK when building the Vulkan backend

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug
```

## Test

```powershell
ctest --test-dir build -C Debug
```

For targeted validation:

```powershell
.\tools\kp.ps1 validate
.\tools\kp.ps1 build RenderPassScheduleTest
```

## Documentation

- [Architecture overview](docs/architecture_overview.md)
- [Project status](docs/status.md)
- [Validation matrix](docs/validation_matrix.md)
- [Graphics/RHI module](docs/graphics/graphics_module.md)
- [Render module](docs/render/overview.md)
- [Asset module](docs/asset/asset_module.md)
- [Command system](docs/command/command_system.md)
- [Gameplay module](docs/gameplay/gameplay_module.md)
- [TTS module](docs/tts/tts_module.md)
- [Live2D module](docs/live2d/README.md)

## Design references

The project studies public designs from open-source engines without copying
their source:

- [gkNextEngine](https://github.com/gameknife/gkNextEngine) — Vulkan-first rendering, GPU submission, and runtime validation.
- [SakuraEngine](https://github.com/SakuraEngine/SakuraEngine) — RHI, render graph, ECS, and editor structure.
- [Piccolo](https://github.com/BoomingTech/Piccolo) — Asset → Resource → Runtime layering.
- [bgfx](https://github.com/bkaradzic/bgfx) — cross-graphics-API abstraction.
- [Godot](https://github.com/godotengine/godot) — production resource and rendering systems.

## Contributing

Issues and pull requests are welcome. Before submitting code, build the
affected targets and run the relevant tests.

## License

This project is licensed under the [MIT License](LICENSE). Third-party
dependencies and the Live2D Cubism SDK are subject to their own licenses.
