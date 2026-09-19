# R4 first consumer: ray-query directional hard shadows

- Status: complete for R4.0; implementation starts in R4.2–R4.3
- Owner: Render/Graphics
- Parent TODO: [render graph R4.0](../../docs/render/render_graph/TODO.md)

## Objective

Choose one bounded ray-tracing consumer before adding common RT contracts. The
consumer must have a visible output, a measurable update boundary, and an
explicit fallback that remains valid on OpenGL and on Vulkan devices without
the complete RT capability set.

## Decision

The first consumer is ray-query hard-shadow visibility for the directional-light
contribution in deferred lighting. It produces one visibility result per
participating shaded sample and is evaluated once per frame when the camera or
participating instance snapshot changes. It reads an imported Graphics-owned
TLAS and directional-light ray policy; it does not build or own acceleration
structures in Render.

The existing directional shadow-map sampling path is the fallback. OpenGL
always uses that path. Vulkan uses it when any required ray-query extension or
feature is unavailable. The fallback is authored as the same graph-level
directional-light branch rather than as an out-of-band pass.

## Alternatives considered

- Ray-query shadows: selected because it exercises AS import, shader-stage
  resource reads, and a small boolean output without requiring a full RT
  pipeline or a new denoiser.
- Ray-traced reflections: deferred because it requires a storage-image output,
  roughness policy, temporal filtering, and a larger composite contract.
- Full ray-tracing pipeline output: deferred because it adds ray-generation,
  miss, hit-group, shader-binding-table, and output-composite contracts before
  the ownership boundary has been proven.

## Vulkan prerequisite contract

The complete path requires Vulkan 1.2-capable hardware exposing:

- `VK_KHR_acceleration_structure`
- `VK_KHR_ray_query`
- `VK_KHR_deferred_host_operations`
- `VK_KHR_buffer_device_address`
- `rayQuery` and `accelerationStructure` device features

The minimum supported contract is all-or-fallback: a device missing any item
selects the directional shadow-map path. Device addresses, native AS handles,
build flags, scratch storage, and retirement remain Graphics-owned.

## R4.0 scope and invariants

- R4.0 selects policy and closes the validation gate; it does not add AS,
  pipeline, shader-binding-table, or dispatch interfaces.
- Render owns consumer selection and graph declarations; Graphics owns physical
  resources, native synchronization, and lifetime.
- Common contracts expose no Vulkan or OpenGL implementation types.
- Vulkan validation errors are observable through an API-neutral diagnostic and
  cause `GraphicsSmoke` to fail after orderly cleanup.
- The existing attachment-scope work is part of the raster baseline and does
  not become an RT synchronization vocabulary.

## Acceptance criteria

- [x] One first consumer, visible output, update boundary, and fallback named.
- [x] Vulkan prerequisite extensions/features and all-or-fallback policy named.
- [x] Runtime smoke cannot report success after a Vulkan validation error.
- [x] Vulkan and OpenGL raster smoke captures remain available as the pre-RT
      baseline.
- [x] R4.0 review records ownership and interface constraints before RT API work.

## Validation plan

Run the focused render/graphics tests, build `GraphicsSmoke`, execute it so it
produces the Vulkan/OpenGL baseline captures, and inspect the working-tree diff
and Markdown links. Full-suite validation is required because the current
attachment-scope work changes common Graphics and Render headers.

## Follow-up

R4.1 must close graph write/update lineage, failed transition propagation,
explicit frame bindings, and buffer requirements before R4.2 adds the common
Graphics RT contract.
