# KimPeanut Engine

**Engine version: 1.3.0**

![KimPeanut Engine](./docs/images/readme-banner.png)

> Chinese documentation: [README.md](README.md)

## About

KimPeanut Engine is a C++ engine research project for real-time rendering. Its
main data flow is explicit: assets enter through import and cook, Resource turns
them into runtime products, Render decides what to draw, and an API-neutral RHI
submits work to Vulkan or OpenGL. The project prioritizes clear ownership, GPU
lifetime, and testable cross-backend boundaries for editor, tooling, and gameplay
prototypes.

See the [architecture overview](docs/architecture_overview.md) for module
relationships, data flow, and ownership boundaries.

# Showcases

### Real-time path tracing and global illumination

The Vulkan path tracer supports progressive accumulation and adaptive SPP. It
uses 1 SPP with guided spatial denoising while the camera moves. After eight
stable camera frames, it uses 4 SPP below 100 accumulated samples, 2 SPP from
100, and 1 SPP from 200, continuing to accumulate across frames for a cleaner
still image. SPP thresholds and rates are configurable, and fixed-SPP mode
remains available. Path tracing is experimental; performance and image quality
depend on the scene and hardware.

<p align="center">
  <img src="./resouce/example/sponza.png" width="49%" alt="Path-traced Sponza scene" />
  <img src="./resouce/example/cornell_box.png" width="49%" alt="Cornell Box global illumination and color bleeding" />
</p>

### Editor and loading screen

<p align="center">
  <img src="./resouce/example/main.png" width="49%" alt="Sponza in the KimPeanut Engine editor" />
  <img src="./resouce/example/loading.png" width="49%" alt="Scene asset loading screen" />
</p>

<p align="center">
  <img src="./resouce/example/live2d.png" width="39%" alt="Live2D Example" />
  <img src="./resouce/example/terrian.png" width="59%" alt="Terrain PCG Example" />
</p>

### Standalone audio player

![KimPeanut Engine standalone audio player](./resouce/example/audio_player.png)

## Features

The main path is `Asset → Resource → Render → RHI`.

### Core capabilities

- **Asset**: `AssetManager` owns typed asset IDs, path deduplication, generation checks, caching, dependencies, and CPU-side lifetime; synchronous and asynchronous loads share one pipeline.
- **Resource / Import / Cook**: Converts CPU assets into native products and render data; imports and cooks models, materials, and textures with dependency closure, image decoding, mipmaps, format policy, and content-addressed output.
- **Render**: Owns RenderWorld, MeshProxy, materials, render targets, fixed pass scheduling, SceneColor, shadows, and debug capture; it does not read arbitrary source files.
- **Ray tracing**: The Vulkan backend provides experimental real-time path tracing and global illumination. Progressive accumulation and adaptive SPP adjust sample rate between camera motion and still views. A Vulkan ray-tracing-capable GPU and driver are required; OpenGL continues to provide raster rendering.
- **Graphics / RHI**: Connects Vulkan and OpenGL through API-neutral handles, descriptions, and `RenderBackend`, owning GPU resources, pipelines, commands, synchronization, and deferred release.

### Supporting systems

- **Runtime**: Provides windowing, input, scripting, gameplay, and core runtime services.
- **Command**: Runtime owns one `CommandRegistry`; the editor console, Lua, tests, and local automation share typed commands with game-thread queuing, structured results, and controlled local JSON-lines transport.
- **Audio**: The miniaudio-backed playback and mixer support synchronous, asynchronous, buffered, and streaming playback, with independent speech, music, and master gain controls. The standalone player imports WAV, MP3, and FLAC into native audio products and uses the played-frame clock to synchronize waveform progress and SRT, WebVTT, or LRC subtitles. See the [Audio module](docs/audio/audio_module.md) for the design.
- **TTS**: Connects to GPT-SoVITS over HTTP through a provider interface, with synchronous or asynchronous requests and buffered or streaming synthesis delivered to AudioSystem for playback.
- **Terrain PCG**: Composable deterministic nodes generate heightfields, derived terrain data, and meshes, with erosion post-processing, shared-renderer previews, and bakeable terrain assets.
- **Editor / optional modules**: Dear ImGui editor support and an optional Live2D import, cook, and runtime product module.
- **Validation**: Unit or contract coverage for Asset, Graphics, Render, Audio, Script, Gameplay, and Command.

## Technology

| Area | Choice |
| --- | --- |
| Language standard | C++20 |
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
- [Audio module](docs/audio/audio_module.md)
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
