# Custom Asset File Structure

Custom engine assets are JSON metadata files with an explicit `version`. Paths
inside a custom asset are relative to that asset's own directory. The Asset
module parses and owns this CPU data; Render later resolves referenced assets
and creates render/GPU state.

```text
asset/
  shader/
    simple_triangle.shader       # shader-program metadata
    simple_triangle.vert         # source referenced by the .shader file
    simple_triangle.frag
  material/
    bootstrap.material           # surface authoring data
  texture/
    wallpaper.jpg
  model/
    sphere/sphere.obj
```

## `.shader`

```json
{
  "version": 1,
  "variants": [{"name": "bound", "defines": []}],
  "shaders": [
    {"stage": "vertex", "format": "glsl", "file": "simple_triangle.vert", "entry": "main"},
    {"stage": "fragment", "format": "glsl", "file": "simple_triangle.frag", "entry": "main"}
  ]
}
```

The loader registers an API-neutral shader-program resource and its stage
resources. Shader processing later derives Vulkan or OpenGL artifacts.

## `.material` (Material Asset V1)

```json
{
  "version": 1,
  "shader": "../shader/simple_triangle.shader",
  "surface": {
    "shading_model": "unlit",
    "blend_mode": "opaque",
    "cull_mode": "back",
    "double_sided": false
  },
  "parameters": {
    "base_color": [1.0, 1.0, 1.0, 1.0],
    "base_color_texture": "../texture/wallpaper.jpg"
  }
}
```

Version 1 accepts only `unlit`, `opaque`/`alpha_blend`, and `none`/`back`/
`front` culling. Parameter values are a scalar, four-number vector, or a
non-empty texture path. Unknown fields and malformed values are rejected so
asset typos cannot silently change rendering. The parsed `MaterialResource`
stores paths and authoring values only; it contains no `AssetID`, render handle,
pipeline, descriptor set, or GPU object. Render resolves those paths to private
AssetIDs, templates, and instances.

The current V1 shader ABI supports one sampled surface texture named
`base_color_texture`; Render maps it to binding 2. Other texture parameter
names are rejected until the material schema grows explicit binding metadata.

## Native `.texture` products and BC formats

Native texture products are canonical little-endian `.texture` containers. The
header records the dimensions, semantic, `TextureFormat`, mip count, and an
embedded integrity digest; the directory records one contiguous payload range
per mip. Runtime validates the directory and digest before exposing the
texture to Resource/Render. Mip payloads are generated from the portable
RGBA source first, then optionally block-compressed during cooking.

The current desktop compressed profile uses these API-neutral formats:

| `TextureFormat` | Block layout | Bytes per 4x4 block | Intended semantic | Sampling rule |
|---|---|---:|---|---|
| `BC4_UNORM` | one BC4 scalar block | 8 | opacity/mask | sampled from `r` |
| `BC5_UNORM` | two BC4 blocks for R and G | 16 | tangent-space normal | sampled from `rg`; reconstruct positive Z |
| `BC3_UNORM` | BC4 alpha + BC1 color | 16 | packed linear channels | sampled as linear |
| `BC3_SRGB` | BC4 alpha + BC1 color | 16 | color/generic LDR data | color RGB is sRGB-decoded by the GPU |

All BC formats use 4x4 blocks, including the edge blocks of non-multiple-of-4
dimensions. The payload size for one mip is therefore
`ceil(width / 4) * ceil(height / 4) * block_bytes`; each subsequent mip uses
the same rule with its own dimensions. A complete RGBA8 mip chain is 4 bytes
per texel, while BC products are 1 byte per texel, so the compressed payload is
approximately one quarter the storage of the portable LDR product before
container overhead.

Normal maps are tangent-space, positive-Z maps. BC5 stores only encoded X/Y in
the red and green channels; the PBR shader decodes them to `[-1, 1]`, applies
the authored normal scale, reconstructs `z = sqrt(max(0, 1 - dot(xy, xy)))`,
and normalizes the result. Object-space normals must not use this BC5 semantic.
Normal mip levels are renormalized before compression so filtering does not
create a non-unit tangent normal. BC compression is lossy; the portable
profile remains available for unsupported backends, HDR data, and quality
comparisons.

The cooker currently maps `Color`/`Generic` to `BC3_SRGB`, `PackedLinear` to
`BC3_UNORM`, `Normal` to `BC5_UNORM`, and `OpacityMask` to `BC4_UNORM` when
`PreferBlockCompression` or `RequireBlockCompression` is selected. Runtime
does not reinterpret compressed bytes as RGBA8. Generated Material V2 products
may publish both products for one texture parameter while retaining the
portable path as the explicit fallback:

```json
{
  "path": "../textures/<portable-hash>.texture",
  "variants": {
    "portable": "../textures/<portable-hash>.texture",
    "bc": "../textures/<bc-hash>.texture"
  },
  "color_space": "srgb",
  "channel": "rgba"
}
```

Runtime derives an API-neutral `TextureVariantProfile` from the initialized
backend before startup Asset dependency resolution. Material loading then
declares exactly one dependency: BC when the complete BC4/BC5/BC3 profile is
supported, otherwise portable. The other path remains metadata for recook and
profile tooling; it is not read during startup. Existing materials with only
`path` remain valid. A backend never silently expands unsupported BC bytes.

## `.material` (Material Asset V2)

Version 2 adds the `standard_pbr` shading model beside `unlit`. The parser
accepts `version ∈ {1, 2}` and rejects anything else; `standard_pbr` is legal
only in version 2.

```json
{
  "version": 2,
  "shader": "../shader/pbr_gbuffer.shader",
  "surface": {
    "shading_model": "standard_pbr",
    "blend_mode": "opaque",
    "cull_mode": "back",
    "double_sided": false
  },
  "parameters": {
    "base_color": [0.8, 0.7, 0.6, 1.0],
    "base_color_texture": "../model/rock1-bl/rock1-albedo.png",
    "normal_texture": "../model/rock1-bl/rock1-normal_ogl.png",
    "metallic": 0.1,
    "metallic_texture": "../model/rock1-bl/rock1-metallic.png",
    "roughness": 0.9,
    "roughness_texture": "../model/rock1-bl/rock1-roughness.png",
    "occlusion": 1.0,
    "occlusion_texture": "../model/rock1-bl/rock1-ao.png",
    "emissive": [0.0, 0.0, 0.0, 1.0]
  }
}
```

The fixed `standard_pbr` semantic set (the canonical constant-block order):

| Semantic | Value type | Texture color space | Default |
|---|---|---|---|
| `base_color` / `base_color_texture` | vec4 / 2D | sRGB → linear | white |
| `normal_texture` | 2D | linear | flat tangent normal |
| `metallic` / `metallic_texture` | float / 2D | linear | 0 |
| `roughness` / `roughness_texture` | float / 2D | linear | 1 |
| `occlusion` / `occlusion_texture` | float / 2D | linear | 1 |
| `emissive` | vec4 | — (constant only) | black |

**Color-space rule:** the intent is per-texture, not per-asset — base color is
sampled through an sRGB texture, the normal/metallic/roughness/occlusion maps
through linear textures. Render keys its GPU-texture cache by
`{asset_id, color_space}`, so one asset sampled in both spaces gets two GPU
textures.

**Texture-wins-over-scalar normalization:** when a semantic authors both, the
scalar is forced to 1.0 (identity multiplier) at resolve time; when only a
scalar is authored, an identity default texture is injected
(`texture/default/default_white.png`, `default_flat_normal.png`); when neither,
the plan-table default applies. The shader always samples all five textures ×
scalars.

The parsed `MaterialResource` still stores paths and authoring values only —
no AssetID, render handle, pipeline, descriptor set, or GPU object. Render
resolves semantics to explicit bindings (base_color=2, normal=5, metallic=6,
roughness=7, occlusion=8; binding 4 is reserved for frame lighting).

StandardPbr authoring values are validated while loading: `base_color`,
`metallic`, `roughness`, and `occlusion` must be finite and within `[0, 1]`;
`emissive` must be finite and non-negative (HDR values are allowed). Unknown
StandardPbr semantics and type mismatches are rejected at the asset boundary.
Material V2 also carries alpha mode/cutoff and texture color-space/channel
metadata. The MI1.5 offline converter emits these fields deterministically;
Render selects packed metallic-roughness G/B channels without splitting the
source image.

## Native `.model` products (Model V3 compact profile)

Native Model products use a canonical little-endian chunk container with a
versioned header, embedded integrity digest, bounds, sections, material hash
references, vertex data, and index data. Model V3 is the compact offline-cooked
profile. It keeps the same decoded `data::Vertex`, section, and material-slot
semantics used by Render, but stores the geometry more densely:

| Data | V3 representation | Runtime reconstruction |
|---|---|---|
| Position | 16-bit unsigned coordinates relative to model Bounds | dequantized `float3` |
| Normal/tangent | signed 16-bit octahedral X/Y pairs | normalized `float3` |
| UV | two IEEE-754 half values | `float2` |
| Bitangent | one handedness byte | normalized cross product of normal/tangent |
| Indices | 16-bit when the compact vertex count is at most 65535, otherwise 32-bit | `uint32_t` |

The V3 vertex record is 24 bytes instead of the V2 56-byte float record. The
importer also reorders vertices by first index use and removes unreferenced
vertices; triangle order, section ranges, material slots, and bounds remain
unchanged. Quantization is bounded and deterministic, so the product hash still
identifies the complete immutable bytes. V3 decode is bounded by the same
native-product and element-count limits as V2 and writes directly into the
existing CPU mesh vectors.

V1 and V2 products remain readable for migration. Reimporting a source with
the current `ModelImportSettings` publishes V3; runtime loading never opens the
foreign source as a fallback when a native product is malformed.

For repeatable product measurements, run
`KimPeanutAssetTool inspect --model <logical-model-key>`. The command reports
the native format version, on-disk vertex/index strides, product bytes, the
decoded CPU payload estimate, geometry counts, and separate file-read and
decode timings. The decoded estimate excludes allocator overhead and GPU
residency; the command does not reimport or modify the archive.

`KimPeanutAssetTool import` and `reimport` also print an `metrics:` block for
AT1 attribution. It includes per-stage wall time, process CPU time and
logical-processor count, peak working set, source/product bytes read, bytes
written, texture binding/cook counts, and product-write count. Fresh AT1.1
imports count one staging write per produced product; the baseline path is
reported as `peak_reserved_bytes: 0 (not budgeted)`, while memory admission
and bounded workers belong to the later AT1.3 stage.

## Multi-section mesh materials

A level static-mesh object may provide an optional `materials` array alongside
its fallback `material` reference. Entries are material assets indexed by the
imported `MeshSection::material_index`; omitted slots use the fallback material.
For the Nanosuit OBJ, Assimp produces material slots 1..6 for Arm, Body, Glass,
Hand, Helmet, and Leg; slot 0 is retained as the fallback entry. Multiple
sections may legitimately reuse one slot (for example glass).
This keeps mesh geometry and material policy separate while allowing models such
as Nanosuit to render each imported surface with its own texture set:

```json
{
  "id": "nanosuit",
  "kind": "static_mesh",
  "transform": {
    "position": [0, 0, 0],
    "rotation_degrees": [0, 0, 0],
    "scale": [1, 1, 1]
  },
  "model": "model/nanosuit/nanosuit",
  "material": "material/nanosuit_arm.material",
  "materials": [
    "material/nanosuit_arm.material",
    "material/nanosuit_arm.material",
    "material/nanosuit_body.material",
    "material/nanosuit_glass.material",
    "material/nanosuit_hand.material",
    "material/nanosuit_helmet.material",
    "material/nanosuit_leg.material"
  ]
}
```

For imported native Models, the Level reference is the extensionless logical
source key. `LevelLoader` resolves it read-only through `asset/.archive` to the
verified hash-named `.model` product; the foreign OBJ/FBX/GLTF/GLB source is
not opened during runtime loading.

## Assimp GLTF/GLB models

`AssetManager` dispatches both `.gltf` and `.glb` files to the existing
`Assimp_ModelLoader`. The loader emits one mesh resource while preserving every
imported section, including the source material slot. Static node transforms
are accumulated and baked into the mesh vertices; normals use the inverse
transpose of the node's linear transform, while tangents and bitangents use
the linear transform and are renormalized.

Each mesh also retains CPU-side `MeshMaterial` metadata: the source name, PBR
factors, alpha mode, and source texture paths for base color, normal,
metallic/roughness, occlusion, and emissive maps. This metadata is intentionally
not a Render material or GPU resource. The current level schema still selects
engine `.material` assets explicitly through `material` and `materials`; the
MI1.5 standalone conversion step can now use the retained metadata to author
those assets, including GLTF's packed occlusion/roughness/metallic map. Runtime
automatic source migration remains MI1.7.
