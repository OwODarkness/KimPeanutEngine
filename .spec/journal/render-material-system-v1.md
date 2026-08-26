# Material System V1

- Status: complete
- Date: 2026-08-26
- Spec: none; this journal records the completed M1–M4 material milestone.
- Parent TODO: [Material System V1 TODO](../../docs/render/material_system_TODO.md)

## What was done

- Replaced the temporary mesh-proxy material identity with render-owned,
  generational material template and instance handles.
- Added immutable surface-template descriptions, compact parameter IDs, and
  typed sparse instance overrides with template-default values.
- Connected material resolution to the render-private resource resolver, which
  supplies common pipeline, texture, and sampler handles without exposing the
  backend to `MaterialSystem`.
- Added pending, ready, and failed material resolution states and opaque versus
  alpha-blend classification.
- Made `FrameContext` turn a ready material instance into frame-local constant
  and texture bindings. The bootstrap renderable now uses a material instance
  instead of an ad-hoc texture binding.
- Added the initial `RenderWorld` / `MeshProxy` consumer path so ready proxies
  draw through Material V1. Sorting remains deferred.

## What changed

- Architecture or behavior: material policy and persistent logical material
  state are owned by `render/MaterialSystem`; static GPU resources stay owned
  by the render-private `RenderResourceResolver`; transient uniform allocations
  and descriptor/resource-binding handles stay owned by `FrameContext`.
- Important files/modules: `engine/runtime/render/material/material_system.*`,
  `frame_context.*`, `render_resource_resolver.*`, `render_system.*`, and
  `render_world/*`; material and render-world coverage lives under
  `engine/test/unit/render/`.
- Public API or ownership changes: `MeshProxy` carries a real
  `MaterialInstanceHandle`. Material instances borrow resolved common handles,
  never native Vulkan/OpenGL objects or frame-local descriptor handles.
  `FrameContext` retires material bindings with its frame slot.

## Validation

- Required level: L3 — the change affects render recording, per-frame resource
  lifetime, and both graphics backends.
- Command: `GraphicsSmoke` (Vulkan and OpenGL, including resize and teardown).
- Result: PASS — recorded by the Material V1 TODO and project status on
  2026-08-26.
- Evidence: [Material System V1 TODO](../../docs/render/material_system_TODO.md)
  records the M4 smoke result; [project status](../../docs/status.md) records
  the same Vulkan/OpenGL smoke coverage. This journal does not claim a new
  rerun while documenting the completed milestone.

## Remaining risks and unverified areas

- Draw-list ordering is currently unsorted; opaque pipeline/material/mesh
  sorting and transparent ordering are still required.
- The V1 material constant layout and binding 3 convention are explicit
  transitional policy, not shader reflection.
- Material V1 has no graph editor, shader generation, bindless resources,
  parameter arrays, push-constant policy, deferred G-buffer encoding, or
  additional material domains.

## Remaining work

- Continue Mesh Proxy MP2/MP3 with world-driven commands, culling, and draw
  list construction.
- Add opaque sorting while preserving the required transparent-order policy.
- Expand toward shadow, G-buffer, lighting, and render-graph work only as
  separately scoped phases.

## Documentation and follow-up

- The design contract is in
  [Material System V1](../../docs/render/material_system.md); the implementation
  ledger remains [material_system_TODO.md](../../docs/render/material_system_TODO.md).
- `docs/status.md` indexes this milestone for future sessions.

## Correction — 2026-08-26

- M5's opaque batching checkpoint landed after this journal's initial entry.
  `SceneDrawListBuilder` now resolves ready visible proxies into separate opaque
  and alpha-blend lists, sorting opaque entries by resolved pipeline handle,
  material instance handle, then mesh handle.
- Alpha-blend candidates still retain snapshot order; camera-depth ordering,
  shadow classification, and later pass-specific draw lists remain open work.
- The targeted `RenderPassScheduleTest` build reconfigured successfully but
  could not compile because MSBuild was denied access to
  `C:\Users\17519\AppData\Local\Microsoft SDKs`.
