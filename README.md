# KimPeanut Engine

![KimPeanut Engine](./docs/images/main.png)

KimPeanut Engine（KP Engine）是一个以渲染器和底层引擎基础设施为核心的 C++ 游戏引擎研发项目。项目重点不是快速堆叠编辑器功能，而是建立清晰的资源、渲染、GPU 生命周期和跨图形 API 边界，为后续游戏玩法、编辑器工具和现代渲染技术提供可维护的基础。

> English documentation: [README.en.md](README.en.md)

## 项目目标

- 构建清晰、可测试、可扩展的 C++ 游戏引擎基础设施。
- 通过 Asset → Resource → Render → RHI 的数据流管理资源和 GPU 对象。
- 以 Vulkan 为主要现代图形 API，同时保留 OpenGL 后端用于跨 API 验证和兼容性实验。
- 将渲染策略、资源处理、GPU 执行和编辑器 UI 解耦。
- 以可验证的增量重构方式演进，而不是依赖隐藏的全局状态或不可追踪的后端行为。

## 项目定位

当前项目处于 active R&D 阶段，核心能力集中在渲染器、资源系统和编辑器基础设施。它不是一个已经稳定发布的商业游戏引擎，也不是只包含单个三角形示例的图形实验；当前工作重点是把可运行的渲染路径逐步重构成拥有明确边界的引擎模块。

## 技术概览

| 项目 | 当前选择 |
|---|---|
| 语言标准 | C++17 |
| 构建系统 | CMake |
| 编译器/平台 | MSVC / Visual Studio 2022，Windows 优先验证 |
| 图形后端 | Vulkan、OpenGL |
| 窗口系统 | GLFW |
| 编辑器 UI | Dear ImGui |
| 模型导入 | Assimp |
| 图像与音频 | stb_image、miniaudio |
| 脚本 | Lua / sol2 |
| 测试 | GoogleTest + CTest |
| Shader 处理 | GLSL → SPIR-V 或 OpenGL 源码，带内容缓存 |

## 当前特性

### 引擎基础设施

- 分层 Runtime / Editor 架构。
- AssetManager 资产身份、缓存、依赖关系和生命周期管理。
- ResourcePipeline CPU 侧资源处理和 Shader 缓存。
- 异步资源请求队列与渲染侧预算消费模型。
- 自定义数学、句柄、事件分发、日志和配置模块。
- Lua VM 宿主层及无窗口单元测试。

### 渲染与 RHI

- Vulkan 和 OpenGL 后端。
- API-neutral 的 Pipeline、Mesh、Texture、Sampler、RenderTarget 和 DescriptorSet 句柄。
- Common CommandRecorder，减少 Render 模块对原生图形 API 的依赖。
- FrameContext 管理每帧临时 uniform 数据和绑定资源。
- RenderPassSchedule、RenderWorld、MeshProxy 和材料系统。
- PBR、纹理材质、深度/颜色目标、基础阴影与延迟渲染方向。
- Vulkan 上传、内存、交换链、描述符和编辑器呈现桥接等后端模块。

### 编辑器与验证

- 基于 ImGui 的编辑器 UI 组件树。
- OpenGL/Vulkan 编辑器呈现接口。
- GraphicsSmoke 跨后端启动、渲染、Resize 和资源释放验证。
- Render、Graphics、Asset、Audio、Script 和 Profile 单元测试。
- 设计文档、状态台账和开源引擎参考研究。

### 命令系统与 Agent 调试

- Runtime 拥有唯一的 `CommandRegistry`；编辑器 `~` 控制台、Lua、测试和
  Agent 都调用同一组已验证命令，命令实现不依赖 ImGui、Lua 或图形后端类型。
- 命令描述其参数、能力和执行线程；Game-thread 命令通过 Runtime 队列执行，
  异步结果以 `request_id` 轮询，避免前端直接访问 Render 或 RHI 对象。
- 运行中的引擎可用 `--agent-port 37373` 开启仅回环的 JSON-lines 端点。Agent
  可用 `--startup-level level/point_shadow_validation.level` 在本次启动选择
  一个已校验的关卡，再执行 `capture.screenshot` 并轮询结果，取得 `SceneColor`
  PNG 用于渲染调试；该选项不会修改 `config/bootstrap.json`。
  `KimPeanutCommand` 只是协议 harness，不启动 Render，不能捕获真实帧。

命令 API、内置命令表和 Agent 请求示例见
[命令系统文档](docs/command/command_system.md)。

## 可选功能模块

### TTS（文本转语音）

TTS 是一个独立的可选模块，不属于核心渲染路径。它通过 provider 接口连接外部语音合成服务，目前实现了基于 HTTP 的 GPT-SoVITS provider，并将结果接入引擎 AudioSystem。

- 同步和异步合成接口。
- 缓冲音频和流式音频两种播放路径。
- 流式响应通过 `AudioStreamDecoder`、FIFO 和 `StreamAudioPlayer` 逐块播放。
- `TTSResult` 返回音频播放器句柄，调用方通过 AudioSystem 控制播放。
- provider 接口为后续云端、本地或离线 TTS 后端保留扩展点。

TTS 需要单独运行的 GPT-SoVITS 服务；当前示例默认使用 `127.0.0.1:9880/tts`。语音参考文件路径由 TTS 服务端解释，不会自动从客户端上传。完整设计、API 和已知限制见 [TTS 模块文档](docs/tts/tts_module.md)。

## 代码规模

截至 **2026-08-26** 的仓库快照：

- 约 **23,348 行** C/C++ 代码。
- **287 个** C/C++ 源文件。
- 统计范围：`engine/` 下的 `.cpp`、`.h`、`.hpp`、`.c`、`.cc`、`.inl`。
- 不包含：`third_party/`、`build/`、`build-opengl-only/`、生成文件和二进制资源。
- 统计包含引擎运行时、编辑器、示例和测试，因此不是“核心库 LOC”。

代码规模是开发快照，不是质量指标；模块边界、可验证性和依赖方向比单纯减少行数更重要。

## 目录结构

```text
KimPeanutEngine/
├── engine/
│   ├── runtime/
│   │   ├── core/          # 数学、基础类型、日志、配置、异步、资源处理
│   │   ├── asset/         # 资产加载、缓存、依赖和生命周期
│   │   ├── graphics/      # RHI 合约与 OpenGL/Vulkan 后端
│   │   ├── render/        # RenderSystem、材质、RenderWorld、Pass、FrameContext
│   │   ├── audio/         # 音频系统与播放器
│   │   ├── input/         # 输入抽象
│   │   ├── script/        # Lua 宿主与脚本边界
│   │   ├── window/        # 窗口系统
│   │   └── bootstrap/     # 启动配置与预加载请求
│   ├── editor/            # 编辑器壳、UI 组件、平台呈现和工具模块
│   ├── example/           # Asset、Graphics、Audio、TTS 等示例
│   ├── module/            # 可选引擎模块
│   └── test/unit/         # GoogleTest 单元与契约测试
├── config/                # 启动配置和运行时设置
├── docs/                  # 架构、状态、设计决策和参考引擎研究
├── third_party/           # 第三方依赖，不属于引擎核心
├── cmake/                 # CMake 辅助逻辑
└── CMakeLists.txt
```

## 架构概览

```text
┌──────────────────────────────────────────────┐
│ Editor                                       │
│ ImGui tools · viewport · logs · settings     │
└──────────────────────┬───────────────────────┘
                       │ editor/runtime seam
┌──────────────────────▼───────────────────────┐
│ Runtime / Engine                             │
│ lifecycle · input · window · gameplay basis  │
└───────────────┬───────────────────┬──────────┘
                │                   │
┌───────────────▼────────┐  ┌──────▼───────────┐
│ Asset / Resource        │  │ Render            │
│ load · process · cache  │  │ scene · material  │
│ CPU-side artifacts      │  │ pass · frame data │
└───────────────┬────────┘  └──────┬───────────┘
                │                   │
                └──────────┬────────┘
                           ▼
                 ┌────────────────────┐
                 │ Graphics RHI        │
                 │ handles · commands  │
                 │ GPU lifetime        │
                 └─────────┬──────────┘
                           ▼
                 ┌────────────────────┐
                 │ OpenGL / Vulkan     │
                 │ API implementation  │
                 └────────────────────┘
```

核心数据流：

```text
Asset file
   → AssetManager
   → ResourcePipeline
   → RenderResource / PipelineDesc
   → RHI resource handle
   → FrameContext + CommandRecorder
   → OpenGL or Vulkan submission
```

关键边界：

- Asset 负责资产身份、加载和 CPU 资源生命周期。
- Resource 负责处理和缓存，不创建 GPU 对象。
- Render 负责场景、材质、Pass、Pipeline 描述和每帧数据。
- Graphics/RHI 负责 GPU 资源、同步和 API 执行。
- Render 和公共 RHI 合约不暴露 Vulkan/OpenGL 原生类型。

## 构建与测试

环境要求：

- Visual Studio 2022，包含 C++ 工作负载。
- CMake。
- Vulkan SDK（构建 Vulkan 后端时需要）。

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

日常开发可使用统一命令包装器，根据 Git 改动自动选择目标验证：

```powershell
.\tools\kp.ps1 validate
.\tools\kp.ps1 status
.\tools\kp.ps1 build RenderPassScheduleTest
```

主要验证目标：

- `GraphicsContractTest` — RHI 合约和管线描述验证。
- `RenderPassScheduleTest` — RenderPass、材质、RenderWorld 和句柄验证。
- `GraphicsSmoke` — OpenGL/Vulkan 共享场景、Resize 和生命周期 smoke test。

## 文档入口

- [项目状态](docs/status.md)
- [架构总览](docs/architecture_overview.md)
- [Graphics/RHI 模块](docs/graphics/graphics_module.md)
- [Render 模块](docs/render/overview.md)
- [命令系统](docs/command/command_system.md)
- [Gameplay 模块](docs/gameplay/gameplay_module.md)
- [Reflection 模块](docs/reflection/PLANS.md)
- [Asset 模块](docs/asset/asset_module.md)
- [TTS 模块](docs/tts/tts_module.md)
- [验证矩阵](docs/validation_matrix.md)
- [Agent 完成证据模板](docs/agent_completion_evidence.md)
- [Spec 与 Journal 工作流](.spec/README.md)
- [Engine reference index](docs/engine-reference/README.md)
- [Agent project contract](AGENTS.md)

## 设计参考

本项目采用“学习模式，不复制源码”的参考方式：

- [gkNextEngine](https://github.com/gameknife/gkNextEngine) — Vulkan-first 渲染、现代 GPU 提交和运行时验证。
- [SakuraEngine](https://github.com/SakuraEngine/SakuraEngine) — RHI、Render Graph、ECS 和编辑器结构。
- [Piccolo](https://github.com/BoomingTech/Piccolo) — Asset → Resource → Runtime 分层。
- [bgfx](https://github.com/bkaradzic/bgfx) — 跨图形 API 抽象。
- [Godot](https://github.com/godotengine/godot) — 生产级资源和渲染系统参考。

## 项目状态

项目持续演进中。当前优先级是完成 Render/RHI 边界重构、增强跨后端验证、完善材质和资源管线，并逐步增加编辑器和 gameplay 基础设施。

实现细节、已知限制和下一步工作以 [`docs/status.md`](docs/status.md) 为准。
