# 2026-11-06 R7.5 combined raster checkpoint

Validated from `8a86bf1` plus the R7.5 changes and the existing working-tree edits, including the separate Sponza startup change. See the [stage review](../../docs/render/.review/R7.5.md) and [execution spec](../specs/render-r7-5-combined-acceptance.md).

## Placement and ownership

Inspected frame transient descriptions, graph attachment operations and dependencies, the fullscreen pipeline helper, and both command recorders. The existing SceneHdr lease, graph versioning, pipeline blend state, and Vulkan image barrier carry the new path. The fog pass keeps its local uniform and shader. The graph executor requests the single-color load through the common recorder. Graphics keeps allocation, synchronization and retirement. The new recorder operation adds no asset or editor dependency.

The broad pre-edit placement scan reported eight existing native includes in four common Graphics implementation files. The scoped final placement scan on the changed sources passed. None of those four files is part of this change. `git diff --check` and Python compilation of the validation harness passed.

## Build and contracts

- `cmake --build build --config Debug --target KimPeanutEngine RenderSystemTest RenderPassScheduleTest RenderGraphTest GraphicsContractTest GraphicsSmoke` built the affected targets across two invocations. The first graph test build found references to removed condition enums in the compatibility test. Removing those stale cases resolved compilation.
- `ctest --test-dir build -C Debug -L 'render|graphics' --output-on-failure` passed 261/261. `build/engine/example/graphics/Debug/GraphicsSmoke.exe` passed six frames per API on the desktop, including its expected invalid-stream rejection.
- `ctest --test-dir build -C Debug --output-on-failure` reported 57 failures and six Live2D skips in the sandbox. An external `ctest --test-dir build -C Debug --rerun-failed --output-on-failure` cleared 48 failures. The same eight Level/Asset failures and one Terrain mesh test recorded by R7.3 persist. The skipped cases were outside the focused run.

## Runtime and images

Launched the engine outside the sandbox on the Windows Default desktop with `--graphics-api <api> --startup-level level/sponza.level --agent-port 37373` and the equivalent `r7_raster_fixture` startup. For procedural terrain used `--mode terrain-viewer`. Ran `python tools/validation/r7/height_fog_validation.py --port 37373 --prefix <unused-prefix> --fixture <fixture> --matrix`, adding `--shadow-mode query` for hybrid Vulkan, `--reference-prefix r74-20261106-<api>-sponza` for Sponza shadow-map comparison, and `--regenerate` for terrain. Captures and JSON use the `r75-20261106-` prefix under `save/screenshots/validation/` and `save/diagnostics/`.

- Debug Vulkan and OpenGL exercised Sponza and the compact fixture. Vulkan also ran hybrid ray-query shadows. Runtime confirmed effect states and shadow modes. No Vulkan VUID was recorded in the normal Debug runs. Both Sponza review runs captured the full composited window with all three effects active and verified the foreground GLFW window on the Default desktop.
- Seed 7426 regenerated the Terrain Viewer preview from revision 0 to 1 on both APIs. Combined and independent-effect captures passed at 1324×781.
- On both APIs, Sponza base-color, fog-off, and transmittance images match R7.4 exactly. Fog-on differs by at most one 8-bit RGB level. Fog disabled and zero density produce identical output on each fixture. The compact fixture passed all five analytic transmittance cases and the sky-bypass check. It also restored the exact initial scene with AO disabled and produced a white AO diagnostic.
- The Vulkan fixed raw PT runs bypassed SSAO, TAA and fog. Accumulation increased from 23 to 105 samples for the shadow-map transition and from 24 to 104 for the ray-query transition, with no toggle-triggered reset in the logs. Returning to the original raster mode restored all three effects and valid TAA history.

## Performance

`cmake --build build --config RelWithDebInfo --target KimPeanutEngine` passed before measurement. The harness ran with `--measure --matrix` on the RTX 4070 Laptop GPU, Vulkan RelWithDebInfo with validation disabled by the normal configuration, using the authored Sponza startup camera at 1094×631 after texture residency settled. Each mode used three windows with 120 warm-up frames and 300 samples. PT was off and Runtime confirmed the actual shadow mode in each window. SSAO Medium, TAA at history weight 0.9, and fog at density 0.06 were active. Builds and tests were idle during sampling.

| Mode | Frame numbers | Total GPU p95 (ms) | Fog GPU p95 (ms) | Active incremental allocation |
| --- | --- | --- | --- | --- |
| Shadow maps | 8103, 8544, 8983 | 3.366, 3.407, 3.401 | 0.022528 each | 32,440,320 bytes each |
| Hybrid ray query | 8453, 8886, 9325 | 3.419, 3.418, 3.447 | 0.021504 each | 32,440,320 bytes each |

The measured difference from the R7.4 allocator count is 5,611,520 bytes. OpenGL reports logical payload only. R7.1 still requires a controlled comparison of material AO overlap and frozen planar/silhouette tolerances. Combined selection-outline interaction and a dedicated orthographic fog capture were outside this checkpoint.
