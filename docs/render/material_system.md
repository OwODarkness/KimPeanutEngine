# Material System V1

**Status: M4 frame-local binding implemented (2026-08-26).** Material System V1 is the
next render reconstruction phase after the initial `MeshProxy` type. M1 has
replaced the temporary `MaterialInstanceHandle` alias in
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
| Material asset/reference selected by a Gameplay component | `asset/` identity, observed by Gameplay |
| Template/instance registry, parameter validation, pipeline request policy | `render/MaterialSystem` |
| Pipeline/static mesh/texture/sampler GPU handles | render-private `RenderResourceResolver`, owned by `RenderSystem` |
| Per-frame uniform ranges and descriptor/resource binding sets | `FrameContext` |
| Native descriptor sets, pipelines, images, samplers | `graphics/` private implementation |

`MaterialInstance` may borrow resolved common `TextureHandle` / `SamplerHandle`
and reference a render pipeline key. It must not own `VkDescriptorSet`,
`VkPipeline`, OpenGL IDs, or a frame-local `DescriptorSetHandle`.

The logical `MaterialInstanceRecord` remains the source of truth for template
identity and overrides. `RenderResourceResolver` keeps separate derived caches
keyed by the same generational handles: template → pipeline handle and instance
→ texture/sampler bindings by `MaterialParameterID`. Constants remain logical
values until `FrameContext` packs them for a frame.

## V1 data model

### `MaterialTemplate`

An immutable, shared description of a surface class. V1 contains only the
information needed to request a pipeline and validate instance data:

```cpp
struct MaterialTemplateDesc
{
    asset::AssetID shader_program;
    MaterialDomain domain;          // Surface only in V1
    MaterialShadingModel shading_model;
    MaterialPipelineState pipeline_state; // blend + culling + double-sided

    MaterialParameterLayout parameters;
    std::vector<MaterialPass> compatible_passes;
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
    std::vector<MaterialParameterOverride> overrides;
};
```

V1 parameter kinds:

- scalar (`float`);
- vector/color (`Vector4f` from the shared math module);
- texture + API-neutral sampler reference (`MaterialTextureSamplerValue`);
- optional normal/base-color/metallic/roughness semantic names for the first
  surface template.

Parameters not declared by the template are rejected. Missing parameters use a
template-defined `MaterialParameterValue` (`std::variant` of those three
types). Parameter names resolve to a compact `MaterialParameterID` when the
template is created; instances and future draw recording use the ID, never a
string lookup. A material instance is stable across frames; its
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

M4 is wired through the bootstrap scene and `GraphicsSmoke`: `RenderScene`
stores a `MaterialInstanceHandle`, asks `FrameContext` for a transient binding,
then records the returned common pipeline and descriptor-set handles. The V1
template declares each texture parameter's descriptor binding explicitly;
binding 3 is reserved for the frame-packed material constant block. `float`
and `Vector4f` constants use the defined scalar/16-byte-vector alignment
layout. This is a temporary V1 binding convention, not shader reflection; a
future serialized template/shader pair decides which constants it consumes.

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

## Material Asset V1

The former `MaterialInstanceHandle` input has been replaced. Material Asset V1
introduces a versioned `*.material` asset containing only stable authoring
values: shader asset reference, surface policy, and typed defaults including
texture asset references. Gameplay selects that asset by `asset::AssetID`;
Render creates and owns one cached derived template/default-instance pair and
all readiness state.
The asset format never stores a pipeline, descriptor set, GPU handle, or
backend-specific value. The detailed migration ledger is M6 in
[material_system_TODO.md](material_system_TODO.md).

## Build order

```text
M1. Material handles + immutable template / instance records (done)
M2. Validate data-driven instance values and logical texture/sampler references (done)
M3. Resolve pipelines/static texture+sampler resources and classify draw intent (done)
M4. Build frame-local binding descriptions through FrameContext (done)
M5. Build RenderWorld proxy registry and draw lists (done)
M6. Material Asset V1: serialized material identity → private template/default instance (done)
M7. Add shadow, G-buffer, lighting, then expand toward a render graph
```

## Completion condition

Material V1 is complete when a renderable can refer to one valid,
render-owned `MaterialInstanceHandle`; Render can derive a common pipeline and
frame-local resource bindings from it; and neither World nor Graphics needs to
know the other's implementation objects.

## Task ledger

Implementation order and acceptance criteria are tracked in
[material_system_TODO.md](material_system_TODO.md).
