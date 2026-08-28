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
