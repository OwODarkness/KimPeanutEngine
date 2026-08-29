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
