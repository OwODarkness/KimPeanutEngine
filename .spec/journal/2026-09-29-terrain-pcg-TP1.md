# Terrain PCG TP1 execution — 2026-09-29

## Scope

Implemented the optional headless data/evaluation stage described by
[`TP1`](../../docs/terrain/.plan/TP1.md). Terrain code has no dependency on
Editor, Runtime, Asset, Render or Graphics. No Perlin/noise operator was added;
generic noise remains outside the terrain domain until a consumer justifies a
Core math/signal API.

## Changes

- Added `TerrainCore` with checked rectangular meter-space domains and
  immutable scalar fields.
- Added versioned recipe JSON, typed operator registration, built-in constant
  scalar source, deterministic DAG validation/evaluation, stable per-node seed
  derivation, and bounded completed-node cache.
- Added an owned worker executor with bounded pending/completion queues,
  revision cancellation and joined shutdown.
- Added `KPENGINE_ENABLE_TERRAIN` (default OFF), optional `Terrain` target, and
  CPU-only `TerrainGenerationTest` target.
- Updated the roadmap/spec/status to distinguish accepted TP1 work from open
  preview, bake and later algorithm stages.

## Validation

- `cmake -S . -B build -G "Visual Studio 17 2022" -DKPENGINE_ENABLE_TERRAIN=ON` —
  configured successfully.
- `.\tools\kp.ps1 build TerrainGenerationTest` — passed.
- `.\tools\kp.ps1 test -l terrain` — 8/8 tests passed.
- Direct `cmake --build` initially failed because the process environment
  contained duplicate case variants `PATH` and `Path`; the repository wrapper
  normalizes the environment and the targeted build succeeded.
- A separate Ninja configure stalled during compiler detection and was stopped.
  Visual Studio project build through the repository wrapper provided the
  successful compiler evidence.
- Reconfigured the same build with `-DKPENGINE_ENABLE_TERRAIN=OFF` and built
  `ModuleBootstrap` through `kp.ps1`; the ordinary optional-module composition
  passed without the Terrain targets enabled.

## Remaining

TP0's complete viewer-publication contract remains open for TP3. TP2 landform
and descriptor operators, TP3 host/preview/bake, and TP4+ erosion/wind/biome
stages remain proposed.
