# KimPeanut Engine

**引擎版本：1.3.0**


![KimPeanut Engine](./docs/images/readme-banner.png)

> English documentation: [README.en.md](README.en.md)

## 项目简介

KimPeanut Engine（KP Engine）是一个基于 **C++20** 开发的实验性 3D 游戏引擎，目标是通过从零实现现代渲染与资源系统，探索游戏引擎底层架构与图形 API 的设计。

引擎目前支持 **Vulkan** 与 **OpenGL** 两种渲染后端，并通过自定义 **RHI（Rendering Hardware Interface）** 隔离上层渲染逻辑与具体图形 API。项目同时包含资源导入与 Cook、GPU 资源管理、渲染管线以及基础引擎框架，并持续向编辑器、工具链和游戏运行时能力扩展。

> KP Engine 更关注工程结构与底层机制的实现，而不是提供完整的商业游戏引擎功能。项目中的许多模块都用于实践和验证现代游戏引擎中的关键问题，例如资源生命周期管理、跨 API 抽象、GPU 内存管理以及可扩展的渲染架构。

完整的模块关系、数据流和所有权边界见[架构总览](docs/architecture_overview.md)。

## 示例与视觉展示

### 实时路径追踪与全局光照

Vulkan 路径追踪支持渐进式采样与自适应 SPP：相机移动时使用 1 SPP 和引导式空间降噪；相机连续稳定 8 帧后，累计样本数低于 100 时使用 4 SPP，达到 100 后降至 2 SPP，达到 200 后降至 1 SPP，并继续跨帧累积以提升静态画质。SPP 阈值和采样率可配置，固定 SPP 模式仍可选。路径追踪目前属于实验性功能，实际性能和画质取决于场景与硬件。

<p align="center">
  <img src="./resouce/example/sponza.png" width="49%" alt="Sponza 场景路径追踪" />
  <img src="./resouce/example/cornell_box.png" width="49%" alt="Cornell Box 全局光照与颜色反弹" />
</p>

### 编辑器与加载界面

<p align="center">
  <img src="./resouce/example/main.png" width="49%" alt="KimPeanut Engine 编辑器中的 Sponza 场景" />
  <img src="./resouce/example/loading.png" width="49%" alt="场景资源加载界面" />
</p>
### Live2D和地形PCG生成器

<p align="center">
  <img src="./resouce/example/live2d.png" width="29%" alt="Live2D 示例" />
  <img src="./resouce/example/terrian.png" width="69%" alt="Terrian PCG 示例" />
</p>

### 音频播放器与TTS

<p align="center">
  <img src="./resouce/example/audio_player.png" width="49%" alt="音频播放器" />
  <img src="./resouce/example/tts.png" width="49%" alt="TTS" />
</p>

## 主要特性

KP Engine 围绕 `Asset → Resource → Render → RHI` 构建清晰的资源与渲染数据流。

### 核心系统

- **Asset**：统一管理类型化资产、缓存、依赖关系与 CPU 生命周期，并提供同步和异步加载。
- **Resource / Import / Cook**：将 Model、Material、Texture 等源资源转换为引擎资产与渲染数据，支持依赖处理、mipmap、格式策略和 content-addressed cooking。
- **Render**：基于 RenderWorld 与 RenderProxy 组织场景渲染，负责材质、RenderTarget、Pass、SceneColor、阴影及调试捕获。
- **Ray Tracing**：Vulkan 后端提供实验性的实时路径追踪与全局光照；渐进式累积和自适应 SPP 在相机移动与静止时切换采样率。需要支持 Vulkan 光追的 GPU 与驱动；OpenGL 路径继续提供光栅化渲染。
- **RHI**：通过 API-neutral handles、descriptors 与 `RenderBackend` 抽象 GPU 资源、Pipeline、Command、同步和生命周期，目前支持 Vulkan 与 OpenGL。

### 引擎与工具

- **Runtime**：窗口、输入、Gameplay、Lua 脚本及基础运行时服务。
- **Command / Agent Interface**：统一的类型化 `CommandRegistry`，供编辑器控制台、Lua、测试、本地自动化和 AI Agent 共用；支持游戏线程调度、结构化结果以及 JSON-lines 本地通信。
- **Audio**：基于 miniaudio 的播放与混音系统，支持同步、异步、缓冲和流式播放，以及独立的语音、音乐和主音量控制。独立播放器支持 WAV、MP3、FLAC 导入与原生音频产品播放，通过播放帧时钟同步波形进度和 SRT、WebVTT、LRC 字幕。详细设计见 [Audio 模块](docs/audio/audio_module.md)。
- **Editor**：基于 Dear ImGui 的编辑器与调试工具。
- **Optional Modules**：
  - Live2D
  - TTS：通过 GPT-SoVITS HTTP 接口进行语音合成，支持同步或异步请求，并可将缓冲音频或流式音频交由 Audio 系统播放。
  - Terrain PCG：通过可组合的确定性节点生成高度场、派生地形数据与网格，并支持侵蚀后处理、共享渲染器预览和可烘焙地形资产。


## 第三方依赖

引擎保留第三方库与引擎代码的边界，当前主要使用：

| 用途 | 主要依赖 |
| --- | --- |
| Asset / Resource | Assimp、stb_image、nlohmann/json、SQLite |
| Render / RHI | Vulkan、shaderc、GLFW、glad、Dear ImGui |
| Audio / TTS | miniaudio、cpp-httplib |
| Script / Reflection | Lua、sol2、EnTT、magic_enum |
| Tests | GoogleTest |

版本、许可证、CMake target 和集成方式见[第三方依赖清单](third_party/README.md)。

## 构建

环境要求：

- Visual Studio 2022 C++ 工作负载
- CMake
- 构建 Vulkan 后端时需要 Vulkan SDK

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug
```

## 测试

```powershell
ctest --test-dir build -C Debug
```

日常开发也可以使用项目命令包装器执行目标验证：

```powershell
.\tools\kp.ps1 validate
.\tools\kp.ps1 build RenderPassScheduleTest
```

## 文档

- [架构总览](docs/architecture_overview.md)
- [项目状态](docs/status.md)
- [验证矩阵](docs/validation_matrix.md)
- [Graphics/RHI 模块](docs/graphics/graphics_module.md)
- [Render 模块](docs/render/overview.md)
- [Asset 模块](docs/asset/asset_module.md)
- [Command 系统](docs/command/command_system.md)
- [Gameplay 模块](docs/gameplay/gameplay_module.md)
- [Audio 模块](docs/audio/audio_module.md)
- [TTS 模块](docs/tts/tts_module.md)
- [Live2D 模块](docs/live2d/README.md)

## 设计参考

本项目参考开源引擎的公开设计，不复制其源码：

- [gkNextEngine](https://github.com/gameknife/gkNextEngine)：Vulkan-first 渲染、GPU 提交和运行时验证。
- [SakuraEngine](https://github.com/SakuraEngine/SakuraEngine)：RHI、Render Graph、ECS 和编辑器结构。
- [Piccolo](https://github.com/BoomingTech/Piccolo)：Asset → Resource → Runtime 分层。
- [bgfx](https://github.com/bkaradzic/bgfx)：跨图形 API 抽象。
- [Godot](https://github.com/godotengine/godot)：资源和渲染系统的工程化实践。

## 贡献

欢迎通过 Issue 或 Pull Request 讨论问题、设计和实现。提交代码前请确保相关目标能够构建，并运行受影响的测试。

## 许可证

本项目使用 [MIT License](LICENSE)。第三方依赖和 Live2D Cubism SDK 受各自许可证约束。
