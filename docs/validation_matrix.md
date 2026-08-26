# Validation Matrix

This document defines the minimum validation required for a change in KimPeanutEngine. It is the shared validation contract for developers, Codex, Claude Code, and CI.

## Principles

- Run the smallest validation that proves the changed behavior.
- Build and test affected targets rather than defaulting to a full build.
- Escalate when a public API, CMake wiring, or dependency boundary makes the impact uncertain.
- Distinguish an environment failure from a source-code failure.
- Runtime rendering changes require runtime evidence; successful compilation alone is insufficient.

## Validation levels

| Level | Purpose | Typical action |
|---|---|---|
| 0 | Documentation-only review | Inspect links, examples, and formatting; no C++ build |
| 1 | Compile affected target | `cmake --build build --config Debug --target <target>` |
| 2 | Unit or contract behavior | Run the affected CTest suite |
| 3 | Runtime smoke behavior | Run an executable such as `GraphicsSmoke` |
| 4 | Broad integration confidence | Full build and complete test suite |

## Path-to-validation matrix

| Changed path or concern | Required minimum validation | Escalate to level 4 when |
|---|---|---|
| `docs/**`, `README*`, `AGENTS.md`, `.claude/**`, `.codex/**` | Level 0 | Documentation changes alter a command, build requirement, or public contract |
| `engine/runtime/core/**` | Level 1 for the affected target; Level 2 for an affected core consumer | A common public header or fundamental handle/type changes |
| `engine/runtime/asset/**` | Level 1 affected asset target; Level 2 asset tests or asset example | Asset identity, loader dispatch, ownership, or public API changes |
| `engine/runtime/audio/**` | Level 1 `AudioUnitTest`; Level 2 `AudioUnitTest` | Audio interfaces or shared runtime headers change |
| `engine/runtime/script/**` | Level 1 `ScriptUnitTest`; Level 2 `ScriptUnitTest` | Script/runtime integration or shared headers change |
| `engine/module/tts/**` | Level 1 `TTSExample` or the consuming target; inspect TTS module tests when added | Provider contract, Audio API, or module wiring changes |
| `engine/runtime/graphics/**` | Level 1 `GraphicsContractTest`; Level 2 `GraphicsContractTest` | Common RHI handles, pipeline descriptions, CMake sources, or public graphics headers change |
| `engine/runtime/graphics/backend/**` | Level 1 `GraphicsContractTest`; Level 3 `GraphicsSmoke` | Backend factory, common RHI contract, frame lifecycle, or cross-backend code changes |
| `engine/runtime/render/**` | Level 1 `RenderPassScheduleTest`; Level 2 `RenderPassScheduleTest`; Level 3 `GraphicsSmoke` | Render public API, renderer/RHI ownership, or a shared header changes |
| `engine/editor/**` | Level 1 editor target; Level 3 editor startup/render smoke when UI behavior changes | Editor/runtime or editor/graphics dependency wiring changes |
| shaders, materials, render targets, or presentation paths | Level 1 affected target; Level 2 relevant render tests; Level 3 runtime smoke or screenshot validation when available | Pipeline layout, shader interfaces, or backend-shared rendering contracts change |
| `CMakeLists.txt`, `cmake/**`, target dependencies, public headers | Reconfigure and build affected targets | The impact cannot be bounded to one module |

## Commands

The repository wrapper provides the standard entry point for agents and developers:

```powershell
.\tools\kp.ps1 validate
.\tools\kp.ps1 validate engine/runtime/render/render_scene.cpp
.\tools\kp.ps1 build RenderPassScheduleTest
.\tools\kp.ps1 test RenderPassScheduleTest
.\tools\kp.ps1 smoke
```

Use the wrapper for normal targeted validation. The raw CMake and CTest commands below remain useful for diagnosis and CI.

Configure when no build tree exists or CMake wiring changed:

```powershell
cmake -S . -B build -G "Visual Studio 17 2022"
```

Build an affected target:

```powershell
cmake --build build --config Debug --target GraphicsContractTest
cmake --build build --config Debug --target RenderPassScheduleTest
cmake --build build --config Debug --target GraphicsSmoke
```

Run a discovered CTest suite:

```powershell
ctest --test-dir build -C Debug -R GraphicsContractTest
ctest --test-dir build -C Debug -R RenderPassScheduleTest
ctest --test-dir build -C Debug -R AudioUnitTest
ctest --test-dir build -C Debug -R ScriptUnitTest
```

Run all tests only for level 4 validation:

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

`GraphicsSmoke` is an executable smoke test. Build it first, then run the produced executable from its CMake target output directory when runtime validation is required.

## Full-validation triggers

Use level 4 validation when any of the following applies:

- A public header shared by multiple modules changes.
- A common RHI handle, `PipelineDesc`, command contract, or render-resource lifetime contract changes.
- CMake target dependencies or library boundaries change.
- A refactor changes Runtime, Render, Graphics, and Editor together.
- The changed-file mapping cannot establish a safe validation scope.
- The user explicitly requests full validation.

## Reporting

Every implementation report should identify:

1. Changed areas.
2. Validation level selected and the reason.
3. Commands run and their results.
4. Checks intentionally skipped.
5. Environment blockers or unverified runtime paths.

Example:

```text
Changed areas: render, graphics
Selected level: 3 — render recording reaches a backend execution path
Passed: RenderPassScheduleTest, GraphicsContractTest, GraphicsSmoke
Skipped: full build — no public API or CMake boundary changed
```
