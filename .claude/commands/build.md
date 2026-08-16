---
description: Build the engine (Debug config)
---

Build the engine in the Debug configuration:

1. Run `cmake --build build --config Debug`.
2. If it fails, report the first error(s) concisely with file:line and stop. Do not start fixing code unless the user asks.

The build tree is `build/` (MSVC, "Visual Studio 17 2022" generator). Third-party DLLs are copied per-target via `copy_thirdparty_dlls` / `copy_runtime_dependencies`. Shaders compile at runtime (via the resource pipeline) — there is no build-time `glslc` step.
