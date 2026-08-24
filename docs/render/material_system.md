# Material System V1

**Status: planned design (2026-08-24).** Material System V1 is the next render
reconstruction phase after the initial `MeshProxy` type. It replaces the
temporary `MaterialInstanceHandle` alias in
[`MeshProxy`](../../engine/runtime/render/render_world/mesh_proxy.h) with a
real render-owned handle system before `RenderWorld` registration and draw-list
work begin.

## Purpose

A material says **how a surface should be drawn**. It is not a GPU object, a
World component, or a render pass. Its job is to connect asset/material intent
to the existing render-owned pipeline cache and common RHI binding descriptors.

```text
World MeshComponent
  → logical material reference
  → Render MaterialSystem resolves it
  → MaterialInstanceHandle
  → MeshProxy / DrawCall
  → FrameContext creates transient bindings
  → CommandRecorder binds common handles
```

## Ownership

| Concern | Owner |
|---|---|
| Material asset/reference selected by a World component | `world/` (future) |
| Template/instance registry, parameter validation, pipeline request policy | `render/MaterialSystem` |
| Pipeline cache and static mesh/texture/sampler GPU handles | `RenderSystem` render caches |
| Per-frame uniform ranges and descriptor/resource binding sets | `FrameContext` |
| Native descriptor sets, pipelines, images, samplers | `graphics/` private implementation |

`MaterialInstance` may borrow resolved common `TextureHandle` / `SamplerHandle`
and reference a render pipeline key. It must not own `VkDescriptorSet`,
`VkPipeline`, OpenGL IDs, or a frame-local `DescriptorSetHandle`.

## V1 data model

### `MaterialTemplate`

An immutable, shared description of a surface class. V1 contains only the
information needed to request a pipeline and validate instance data:

```cpp
struct MaterialTemplateDesc
{
    asset::AssetID shader_program;
    MaterialDomain domain;          // Surface only in V1
    MaterialBlendMode blend_mode;   // Opaque or alpha-blend
    bool double_sided = false;

    MaterialParameterLayout parameters;
    PipelineStateDesc pipeline_state;
};
```

The exact C++ names may change. The invariants may not: template state is shared
and immutable after creation; it maps to one render-side pipeline-cache request
per compatible pass/attachment signature; Graphics only receives the completed
`PipelineDesc`.

### `MaterialInstance`

An instance selects a template and supplies values for that template's declared
parameters:

```cpp
struct MaterialInstanceDesc
{
    MaterialTemplateHandle template_handle;
    MaterialParameterValues values;
};
```

V1 parameter kinds:

- scalar (`float`);
- vector/color (`float2`, `float3`, `float4` once the shared math/value type is
  selected);
- texture + sampler reference;
- optional normal/base-color/metallic/roughness semantic names for the first
  surface template.

Parameters not declared by the template are rejected. Missing parameters use a
template-defined default. A material instance is stable across frames; its
resolved GPU binding set is not.

## Binding lifetime

This is the boundary that avoids recreating the old renderer's persistent
descriptor ownership:

```text
MaterialInstance (persistent)
  template + parameter values + borrowed static texture/sampler handles
                 │
                 │ each frame
                 ▼
FrameContext
  allocates material uniform range + ResourceBindingSetHandle
                 │
                 ▼
CommandRecorder
  BindPipeline + BindResourceBindingSet + BindMesh + DrawIndexed
```

The material system prepares a `ResourceBindingSetDesc`; `FrameContext` owns the
returned binding set and releases it when the frame slot retires. An instance
must never cache that returned handle.

## V1 scope

Material System V1 must provide:

- generational `MaterialTemplateHandle` and `MaterialInstanceHandle`;
- template and instance create/destroy/update validation;
- render-side texture/sampler/pipeline resolution through existing caches;
- opaque and alpha-blend classification for future draw lists;
- a method to build frame-local material binding descriptions;
- headless tests for stale handles, template-instance lifetime, parameter
  validation, defaults, and frame-binding lifetime boundaries.

V1 explicitly does **not** provide:

- node graphs, shader generation, or a material editor;
- arbitrary parameter arrays, bindless, push-constant policy, or virtual
  textures;
- deferred-specific G-buffer encoding or a PBR lighting implementation;
- native Vulkan/OpenGL material classes;
- World/component ownership of graphics handles.

## Build order

```text
M1. Material handles + immutable template / instance records
M2. Validate values and resolve static texture/sampler/pipeline dependencies
M3. Build frame-local binding descriptions through FrameContext
M4. Replace MeshProxy's temporary material alias with MaterialInstanceHandle
M5. Build RenderWorld proxy registry and draw lists
M6. Add shadow, G-buffer, lighting, then expand toward a render graph
```

## Completion condition

Material V1 is complete when a renderable can refer to one valid,
render-owned `MaterialInstanceHandle`; Render can derive a common pipeline and
frame-local resource bindings from it; and neither World nor Graphics needs to
know the other's implementation objects.
