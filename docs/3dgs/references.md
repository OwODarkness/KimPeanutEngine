# 3DGS Source and Dependency Study

**Study date: 2026-09-30.** This is a design study; no external source was
imported. [Architecture](PLANS.md) and [roadmap](TODO.md) own the decisions.

## Local engine observations

- `engine/module/3dgs/3dgs_model.h` has only an empty placeholder and the
  directory is absent from `engine/module/CMakeLists.txt`.
- `engine/runtime/graphics/backend/common/buffer_types.h`,
  `render_backend.h`, `pipeline_types.h`, and `command_recorder.h` expose
  per-instance vertex bindings, per-frame buffer writes, graphics pipelines,
  and indexed instanced draws. `CommandRecorder` has ray dispatch but no
  general compute dispatch, and `PipelineDesc` has no compute shader field.
- `engine/runtime/render/render_pass_declaration.cpp` compiles distinct raster
  and PT branches. Raster lighting writes `SceneHdr`, then ToneMap reads it.
  `renderer_frame_targets.cpp` gives `SceneHdr` a clear-on-begin HDR color
  target without depth. The GBuffer has a sampled depth attachment.
- Asset's existing module registration seam and Live2D/terrain optional module
  layouts are more suitable than inserting Gaussian parsing into generic mesh
  import. Assimp/meshoptimizer do not solve GraphDeco custom-property decoding
  or ordered alpha splatting.

## Source observations

| Reference | Observed source | Transfer to this engine |
| --- | --- | --- |
| GraphDeco `gaussian-splatting`, local `54c035f7834b564019656c3e3fcc3646292f727d` | `scene/gaussian_model.py` `save_ply`/`load_ply` stores `x/y/z`, `f_dc_*`, `f_rest_*`, `opacity`, `scale_*`, `rot_*`; activations are sigmoid/exp/quaternion normalization. Local checkout has edits in `gaussian_renderer/__init__.py` and `scene/dataset_readers.py`, so study only the named model methods. | Treat this as the first import schema and image/math reference; keep the checkout read-only. |
| GraphDeco SIBR viewer, local submodule at `d8856f6` | `src/projects/gaussianviewer/renderer/GaussianView.cpp` loads PLY, allocates CUDA arrays, registers a GL buffer for CUDA/GL interop, and copies/presents the CUDA result. | Confirms CUDA can help render, but its interop and application framework are larger than the engine's first slice. |
| gkNextEngine `4ba5b7cd106c282e7ed166ff853aeea87b392680` | [`SplatModule.cpp`](https://github.com/gameknife/gkNextEngine/blob/4ba5b7cd106c282e7ed166ff853aeea87b392680/src/Modules/SplatLoader/SplatModule.cpp) registers `.sog` loading and an external pass. [`FSogLoader.cpp`](https://github.com/gameknife/gkNextEngine/blob/4ba5b7cd106c282e7ed166ff853aeea87b392680/src/Modules/SplatLoader/FSogLoader.cpp) decodes ZIP/WebP SOG into CPU splat data. [`GaussianSplatPass.cpp`](https://github.com/gameknife/gkNextEngine/blob/4ba5b7cd106c282e7ed166ff853aeea87b392680/src/Modules/SplatLoader/GaussianSplatPass.cpp) uses GPU bucket sorting, indirect billboard draw into a splat target, then a compute composition into scene color, with scene depth read-only. | Adopt the separated loader, instance, accumulation, and composition responsibilities. Do not copy Vulkan handles or external-pass API into the common Graphics contract. gk's SOG input differs from the user's GraphDeco PLY output. |
| Khronos glTF [`KHR_gaussian_splatting`](https://github.com/KhronosGroup/glTF/blob/main/extensions/2.0/Khronos/KHR_gaussian_splatting/README.md) | Ratified glTF 2.0 extension describes point primitives with Gaussian position, rotation, scale, opacity, and SH fields. | Consider as future interchange, once PLY rendering works; do not require glTF conversion for GS1. |
| Khronos [Vulkan Gaussian sample](https://github.khronos.org/Vulkan-Site/samples/latest/samples/complex/render_octomap/Tutorials/gaussian-splats-rendering.html) | Projects anisotropic splats to quads and uses ordered premultiplied alpha; sample docs call out approximate demonstration content. | Useful independent math/ordering check, not a full quality target or a ready engine module. |
| NVIDIA [`vk_gaussian_splatting`](https://github.com/nvpro-samples/vk_gaussian_splatting) | Vulkan reference viewer offers multiple Gaussian rendering methods and identifies `miniply`, `vrdx`, and `spz` as separate libraries. | Compare visual/performance tradeoffs in GS3; importing the whole viewer would duplicate this engine's renderer. |

## Dependency fit

| Candidate | Fit now | Decision |
| --- | --- | --- |
| [`tinyply`](https://github.com/ddiakopoulos/tinyply) | C++17 parser for binary/ASCII PLY, arbitrary properties and large files; public-domain/BSD-2 fallback stated by upstream. | Preferred small **import-only** dependency after GS0 parser and license probe. Wrap behind module importer; do not expose its types in the CPU model. |
| Existing Assimp / meshoptimizer | Mesh import/optimization, but the required named SH/log-scale/logit fields and order need a Gaussian-aware path. | Keep for meshes; do not use as the 3DGS schema authority. |
| GraphDeco `diff-gaussian-rasterization`, `simple-knn`, SIBR | Training/CUDA rasterizer and a separate viewer; GraphDeco license is research/evaluation-only with redistribution terms. | Reference implementation only. Do not vendor it or make it a runtime dependency. |
| [AMD FidelityFX Parallel Sort](https://github.com/GPUOpen-Effects/FidelityFX-ParallelSort) | MIT GPU sort, Vulkan/D3D12 compute shader path. | GS3 candidate if profiling supports GPU sorting and the Graphics compute/hazard contract is added. |
| CUDA Toolkit + [`CUB::DeviceRadixSort`](https://github.com/NVIDIA/cccl/blob/main/cub/cub/device/device_radix_sort.cuh) | Strong NVIDIA sort option and a natural later trainer dependency. CUB file is BSD-3; CUDA toolkit has its own distribution terms. | GS3 optional CUDA accelerator, loaded only when a compatible NVIDIA device/toolkit is available. Keep a non-CUDA renderer path. |
| PlayCanvas [SOG format](https://developer.playcanvas.com/user-manual/gaussian-splatting/editing/supersplat/) | Compact ZIP/WebP delivery format used by gkNextEngine; it adds ZIP, WebP, quantization, and metadata handling. | Future import/streaming stage, after PLY and composition are correct. |

## CUDA integration rule

CUDA can accelerate sort/culling or later training without owning the scene or
render graph. A CUDA adapter should consume a packed Gaussian buffer and emit
sorted indices/attributes into a Graphics-owned frame resource. For Vulkan,
NVIDIA's [interop guide](https://docs.nvidia.com/cuda/cuda-programming-guide/04-special-topics/graphics-interop.html)
requires a matching device, exported memory and semaphores, and explicit
wait/signal order. OpenGL interop uses registration and map/unmap; GL must not
access a resource while CUDA has it mapped. The module must report unsupported
devices and fall back explicitly. Include resource retirement, resize, device
loss, and cross-API parity in that stage's acceptance before calling it ready.
