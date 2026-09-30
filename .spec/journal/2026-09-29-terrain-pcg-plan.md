# Terrain PCG planning — 2026-09-29

- Task: inspect the user's Pangolin terrain prototype and plan a stronger
  engine module, viewer and extensible generation architecture.
- State: planning complete; implementation not started.
- Baseline: `cc2cde7397a318fff2e39ff6ad8350ce6da4d66c`; initial working tree clean.
- Roadmap: [terrain TODO](../../docs/terrain/TODO.md).
- Spec: [terrain authoring](../specs/terrain-pcg-authoring.md).

## Confirmed user direction

Heightfield terrain comes first; author in a tool and bake into engine assets.
The evaluator must support added algorithms and later other geometry models,
including wind effects. User clarified that "ray erosion" means rain/hydraulic
erosion. Noise-only generation is insufficient.

## Investigation and decisions

Read status, project/validation contracts, optional Live2D module/host,
application-host registry, Scene3D coordinator/mode gates, Asset import/type
registries, mesh/native-model contracts, prepared Render catalog, logical
sources, and Runtime Level reload implementation. Observed that host addition
requires 3D composition work and Level reload does not publish new mesh assets.

Read selected Pangolin generator, worker, procedural actor, toolkit and viewport
source at `d7618b293ba8193706c58df3265e0a2802e5a47e`. Identified generation/texture
coupling, shared worker mutation/lifetime concerns, seed bypasses and square
dimension assumptions. Findings are source inspection, not UE runtime evidence.

Surveyed primary research for drainage/uplift, grid hydraulics, FastFlow,
controlled erosion detail, windblown sand, terrain descriptors and learned
terrain synthesis. Added a 2026 stochastic transport candidate; inspected its
public transport interface/kernel at
`97e893502f7c2e45e4ffa3c93518a7dfd9aa9454`, not the full erosion implementation.
Inspected HTerrainData channels/invalidation at
`7f574eb47fbd74cb1a79adc2cc9fb7f0694fccc3`. Detailed findings and links live in
[references](../../docs/terrain/references.md).

Chose a proposed optional compiled module, typed CPU evaluation DAG, immutable
results, Asset-owned native bake integration and dedicated workspace sharing
3D rendering. Separate mesh capabilities from heightfield solvers. Establish
the working Scene3D workspace before making a standalone host a prerequisite.

## Changed areas and ownership

Documentation only: terrain architecture/roadmap/agent guide/references,
execution spec and this journal, plus status/reference-index discovery links.
No runtime/CMake/API/ownership changes were implemented; the documents assign
proposed owners and explicitly record prerequisite gaps.

## Validation and limits

Level 0 documentation validation applies. Link/formatting and diff results
are recorded below after the final check. No C++ build, engine launch, UE run,
solver experiment or performance measurement is required for this planning
change. Runtime visuals, resource lifetime, numerical behavior and all proposed
targets remain unverified until their stages are implemented.

Public-source access initially failed in the sandbox; approved read-only
network access succeeded. Web fetches could not retrieve Pangolin; raw source
requests occasionally hit TLS/EOF errors and successful decoded reads were
used. No external source was copied into the engine or vendored.

## Final documentation checks

- `git diff --check`: passed; Git reported only normal LF/CRLF conversion notices.
- Local Markdown link resolution across the six new documents: zero missing
  targets. Trailing-whitespace scan: zero offending lines.
- Reviewed status/reference-index additions and new architecture, roadmap,
  reference, spec and journal documents. All implementation stages remain open.
- Build/runtime checks skipped because only documentation changed; no source,
  CMake or executable behavior was modified. No environment blocker remains
  for delivery of this plan.

## Correction — dedicated mode first

User clarified that direct Scene3D authoring integration would add debugging
burden. Supersedes the earlier Scene3D-workspace-first sequencing: TP3 now
starts with a dedicated `terrain-viewer` host and fixed preview scene, sharing
neutral 3D rendering services. Ordinary Scene3D startup must not construct the
terrain authoring session or generation jobs. Normal Scene3D remains the baked
asset integration check after viewer validation. If host composition is
deferred, TP3 remains open; a Scene3D authoring panel is no longer the fallback.

Updated PLANS, TODO, terrain AGENTS, spec and status to match. Added planned
replay, operator isolation, intermediate-field snapshots, pause/step and
per-node time/memory/numerical diagnostics. Only documentation changed;
runtime ownership and executable behavior remain unchanged and unverified.

## Follow-up — voxel terrain deferred

User asked about future voxel terrain, then explicitly requested only marking
it as further work. Added a deferred roadmap entry; no voxel stage design,
implementation, algorithm choice or release prerequisite was introduced.
Initial scope remains heightfield authoring/baking in dedicated terrain mode.
Documentation only; `git diff --check` passed for this update.

## Follow-up — data-only PCG and Gameplay reuse

User clarified during TP2 work that terrain should use Actor representation
and reuse Gameplay/Render while PCG deals with data. Inspected current terrain
CMake, TP2 stage/roadmap, Gameplay static-mesh factory/MeshComponent and world
ownership contracts. TerrainCore/Generation directly link Data/Math/JSON, not
Gameplay or Render. Added the explicit split: CPU result evaluation stays
headless; viewer/runtime adapters register/prepare assets, then create or update
Gameplay-owned Actors/components feeding existing Render source contracts.
A minimal preview GameplayWorld is compatible with skipping game Level startup.

Updated terrain PLANS, AGENTS, TP2 stage, TODO and spec; preserved implementation
and acceptance state from the ongoing TP2 work. No source or CMake edits and no
messages were sent to the other agent. Generated preview catalog publication
remains an open integration prerequisite. Documentation checks are reported
in this chat; builds/runtime checks are unnecessary for this documentation edit.
