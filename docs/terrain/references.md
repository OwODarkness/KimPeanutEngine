# Terrain source study and algorithm selection

Study date: 2026-09-29. This is a design study, not an execution benchmark or a
full paper reproduction. Architecture: [PLANS.md](PLANS.md). Roadmap:
[TODO.md](TODO.md). Sources were inspected selectively; no source was imported.

## Pangolin baseline

Repository: `OwODarkness/PangolinProject`, `main` at
`d7618b293ba8193706c58df3265e0a2802e5a47e`.

Inspected paths beneath `Plugins/PangonlinSupport/Source/`:

- [PangolinTerrain.h](https://github.com/OwODarkness/PangolinProject/blob/d7618b293ba8193706c58df3265e0a2802e5a47e/Plugins/PangonlinSupport/Source/PangonlinSupport/Public/PangolinTerrain.h)
  and its `.cpp`: Perlin/fBm, diamond-square, convolution, coastal erosion,
  color-layer rules, timing and texture generation share one UObject.
- [CoastErosionRunnable.cpp](https://github.com/OwODarkness/PangolinProject/blob/d7618b293ba8193706c58df3265e0a2802e5a47e/Plugins/PangonlinSupport/Source/PangonlinSupport/Private/CoastErosionRunnable.cpp)
  and header: raw pointer to mutable Terrain, global random calls, empty Stop,
  completion delegate invoked by worker Exit.
- [TerrainActor.cpp](https://github.com/OwODarkness/PangolinProject/blob/d7618b293ba8193706c58df3265e0a2802e5a47e/Plugins/PangonlinSupport/Source/PangonlinSupport/Private/TerrainActor.cpp):
  one procedural mesh section, fixed sampling stride and height curve.
- Editor `PangolinEditorToolkit.cpp` and `PangolinEditorViewportClient.cpp`:
  generation button refreshes textures, plot and preview actor after calling
  GenerateHeightMap. Separate runtime/editor module organization is useful.

Findings from the inspected code, not runtime reproduction:

1. `CoastErosion` creates per-direction workers on the same array, drops local
   worker/thread ownership, and sleeps 50 ms. GenerateHeightMap then proceeds
   to convolution/texture work. There is no visible join or array lock in the
   inspected path, so overlapping read/write and partial preview are risks.
   The completion counter assumes four workers even when directions are disabled.
2. Diamond-square constructs a seeded FRandomStream but uses global
   `FMath::RandRange`; erosion also uses global randomness. Perlin seed mapping
   reduces its seed hash modulo 100 and uses a coordinate offset. Configured
   seed alone therefore does not establish reproducible generation.
3. Generation and actor code infer square dimensions; mesh sampling and
   rendering controls are coupled. `GetHeightMap` returns the entire array
   by value. No general typed intermediate result or chunk boundary contract
   appears in these inspected paths.

Keep the terrain asset/preview idea and control maps. Replace worker ownership,
mutation, seed handling and evaluation/data boundaries in the new design.
This review does not establish every defect in the old project.

## Architectural precedent

`Zylann/godot_heightmap_plugin`, `master` at
`7f574eb47fbd74cb1a79adc2cc9fb7f0694fccc3`:
[addons/zylann.hterrain/hterrain_data.gd](https://github.com/Zylann/godot_heightmap_plugin/blob/7f574eb47fbd74cb1a79adc2cc9fb7f0694fccc3/addons/zylann.hterrain/hterrain_data.gd).
Its Resource separates height, normal, splat, color and detail channels, tracks
formats/color space, and signals region/resolution changes. Adopt explicit
channels and invalidation; adapt them to immutable CPU fields and Asset-owned
products. Do not copy its Godot texture ownership into TerrainCore.

Local Live2D precedent:
[host](../../engine/module/live2d/live2d_viewer_host.h),
[CMake](../../engine/module/live2d/CMakeLists.txt),
[bootstrap](../../engine/module/module_bootstrap.cpp). It demonstrates optional
libraries, asset registration and a dedicated host. Its direct viewer rendering
does not use the 3D RenderWorld/DeferredRenderer; terrain needs that 3D path.
Adopt composition boundaries, not its full viewer implementation.

## Algorithm selection

These are candidate adaptations, not promises of scientific fidelity or
measured KimPeanutEngine performance.

| Method / primary source | Useful result | Proposed role and limitation |
| --- | --- | --- |
| [Uplift + fluvial erosion, 2016](https://onlinelibrary.wiley.com/doi/10.1111/cgf.12820) | Large relief and branching drainage with high-level controls | CPU macro-landform baseline; older, useful model; not a full short-time water solver |
| [FastFlow, 2024](https://pubs.cs.uct.ac.za/id/eprint/1774/) | Flow/depression routing and GPU landscape simulation | TP2/TP4 routing design, later acceleration; GPU algorithm needs a separate RHI/compute evaluation |
| [Hydraulic erosion, Mei et al., 2007](https://evasion.imag.fr/Publications/2007/MDH07/) | Grid water flow, erosion/deposition and sediment transport | Conservative local rain-erosion baseline to compare; mature rather than recent |
| [Stochastic geomorphological transport, 2026](https://erosiv.studio/publications/stochastic-geomorphological-transport) | Momentum-aware transport; reported meanders, braided rivers and deposits | TP4 research candidate; implementation/numerical audit required before adoption |
| [Controlled procedural patterns, 2024](https://onlinelibrary.wiley.com/doi/10.1111/cgf.14992) | Terrain-oriented cascaded erosion-like detail using structured noise | Optional fast detail operator; does not simulate conserved water/sediment |
| [Windblown sand around obstacles, 2024](https://www-sop.inria.fr/reves/Basilic/2024/RDBC24/) | Wind flow coupled to saltation and avalanching, compared with observed patterns | Wind quality-tier reference; paper uses 3D airflow with heightfield sand updates, not arbitrary mesh weathering |
| [Terrain descriptors, 2025](https://onlinelibrary.wiley.com/doi/10.1111/cgf.70080) | Metrics for slope, curvature, visibility, hydrology and landforms | Diagnostic fields, biome controls and quality evaluation; a survey, not a new generator |
| [Terrain Diffusion Network, 2024](https://ojs.aaai.org/index.php/AAAI/article/view/29150) | Sketch/climate-guided learned terrain synthesis | Future optional source operator; model deployment, training data and reproducibility are additional concerns |

Suggested first implementation is controls -> coherent drainage -> physically
interpretable erosion -> descriptors -> materials/scatter. Keep noise as one
source/detail operator. Compare the stochastic method as a potential successor
or alternate solver; do not stack multiple erosion methods without establishing
their time scales and effects on the same sediment budget.

### Inspected 2026 research source

`erosiv/geotransport`, `main` at
`97e893502f7c2e45e4ffa3c93518a7dfd9aa9454`:
[source/geotransport/path.hpp](https://github.com/erosiv/geotransport/blob/97e893502f7c2e45e4ffa3c93518a7dfd9aa9454/source/geotransport/path.hpp),
[path.cu](https://github.com/erosiv/geotransport/blob/97e893502f7c2e45e4ffa3c93518a7dfd9aa9454/source/geotransport/path.cu),
and [README](https://github.com/erosiv/geotransport).

The interface takes flow/source/decay fields, explicit RNG state, physical
cell scale and sample count. The inspected CUDA kernel integrates stochastic
paths on a 2D field and uses atomic accumulation. This supports separating
transport fields/state from geometry presentation. It also makes deterministic
parallel reductions an explicit issue. The README says this repository is the
transport reference, with the full erosion implementation in a separate
`soillib` project; that full implementation was not audited here. CUDA/silt and
Python binding choices are not adopted as engine dependencies.

The paper's external force-field example motivates reusable force inputs,
but does not establish a calibrated aeolian model for arbitrary geometry.
Wind erosion, mesh abrasion and foliage motion remain separate implementations.

## Study limits and next experiments

Pangolin and reference source was read via public GitHub APIs/raw URLs; the
web fetcher could not retrieve the Pangolin repository. Some raw requests had
transient TLS/EOF failures. Successful decoded source reads support the findings
above. Papers were surveyed through primary publication/author pages and
available extracts; no paper benchmark or numerical reproduction was run.

Before TP4 selection, read the complete selected papers and solver source,
derive the units/conservation model, and run matched small fixtures. Compare
resolution/timestep sensitivity, water/sediment balance, drainage correctness,
artist control, peak memory and generation latency. Scientific realism cannot
be accepted from attractive screenshots alone.
