# 3DGS Module Guide

Read the repository [agent contract](../../AGENTS.md), [architecture](PLANS.md),
[roadmap](TODO.md), [source study](references.md), and the relevant stage plan
before changing `engine/module/3dgs`.

## Boundaries

- The module owns Gaussian data validation, conversion policy, and splat draw
  preparation. Asset owns source identity, CPU payload lifetime, and loading.
  Resource processing may produce a packed CPU artifact. Render owns pass order,
  graph resources, camera policy, and composition. Graphics owns GPU resources,
  synchronization, and backend execution.
- Runtime and common Graphics contracts must not expose CUDA, SIBR, Vulkan, or
  OpenGL implementation types. The module must build out when disabled.
- Do not route a PLY through generic mesh import: Gaussian-specific properties,
  activation functions, and spherical harmonics must survive import.
- Each GPU buffer/target needs one owner and frame-safe retirement. Never retain
  a raw pointer to a transient graph target beyond its frame.
- Keep the user's external GraphDeco checkout a read-only reference. Do not
  copy its renderer or model source into this repository; its license restricts
  use and redistribution. Pin and audit any new third-party dependency before
  adding it under `third_party/`.
- Training, gradients, optimizers, and CUDA interop are outside the render-only
  stages. Preserve a versioned model contract so a later trainer can produce the
  same import input or native product.

## Validation

Follow [the validation matrix](../validation_matrix.md). For render changes,
use Debug Vulkan validation and a checked-in startup fixture, Runtime command
capture under `save/screenshots/validation/`, and then OpenGL where supported.
Launch the engine outside the sandbox on the Default desktop. Measure only a
fresh RelWithDebInfo build under matched camera, fixture, mode, and resolution.
Record PLY count, SH degree, visible count, sort/upload/draw cost, and GPU
memory with the result. See [GS1](.plan/GS1.md) for the first slice's gates.

## Document ownership

- `PLANS.md`: durable architecture and decisions.
- `TODO.md`: current stages and acceptance.
- `.plan/GS*.md`: concrete stage designs.
- `references.md`: pinned source observations and dependency comparison.
- `.spec/journal/`: implementation evidence after work starts.
