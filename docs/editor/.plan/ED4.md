# ED4 — Loading Wireframe Icosahedron

- Status: planned
- Parent design: [Editor Module](../editor_module.md)
- Roadmap: [Editor TODO](../TODO.md)
- Builds on: [Asset LO3](../../asset/.plan/LO3.md) loading-safe presentation

## Objective

Use the currently quiet space to the left of the startup card for a rotating
wireframe icosahedron that strengthens the loading screen's cyber/technical
identity. The visual must remain available during the earliest loading frames,
before scene rendering or render targets exist, and must behave identically on
OpenGL and Vulkan through the existing ImGui presentation path.

ED4 is complete when the real loading screen shows a stable, smoothly rotating
icosahedron in the available left rail, remains readable at supported window
sizes, and introduces no dependency from Editor UI to Render or Graphics
resource ownership.

## Design question

Should the loading screen sample an offscreen render target, or project the
icosahedron on the CPU and submit its edges to `ImDrawList`?

ED4 chooses **CPU projection plus `ImDrawList::AddLine`**. An icosahedron has
only 12 vertices and 30 unique edges, so a render target would add pipeline,
texture, descriptor, resize, synchronization, and backend-lifetime work without
providing useful capability for this stage. More importantly, Asset LO3 makes
the loading tree safe before scene rendering exists. The decoration must not
weaken that contract.

A future shader-driven glitch stage may introduce an explicitly owned
presentation effect target if it needs full-image feedback, block corruption,
bloom, or post-process chromatic separation. That is not an ED4 prerequisite.

## Scope boundary

ED4 owns:

- one immutable icosahedron topology: normalized model-space vertices, triangular
  faces, and 30 deduplicated edges;
- presentation-local rotation, perspective projection, edge visibility, and
  screen-space placement;
- ImGui draw-list submission and clipping inside the computed visual rail;
- responsive suppression when the left rail is too small to be intentional;
- loading, ready, closing, and failed-state color/style selection;
- focused projection/topology tests and Vulkan/OpenGL visual evidence.

ED4 does **not** own:

- GPU meshes, shaders, materials, render passes, render targets, or scene cameras;
- changes to Runtime startup progress, Asset scheduling, or the loading view model;
- a general 3D widget or reusable debug-drawing framework;
- relocation of the Startup Profiler or adoption of the workspace layout model by
  the loading tree;
- the full Cyberpunk-style error/post-processing effect.

## Ownership and dependency boundary

`EditorLoadingComponent` remains the owner and lifetime boundary. The geometry
and animation are presentation-local values rebuilt or transformed per frame;
no state is published to Runtime, Render, or Graphics. Time comes from the
current ImGui presentation clock, as the existing grid and indeterminate
progress animation already do.

If projection math is extracted for testing, it stays under
`engine/editor/ui/component/` and uses small Editor-owned plain data types. It
must not enter `engine/runtime/core`, because there is only one present consumer
and no engine-wide geometry contract is justified.

The draw path receives only a draw list, a clip rectangle, elapsed time, and a
small visual state derived from the existing loading view model. It must not
retain an `ImDrawList*`, borrow Runtime objects, allocate GPU resources, or read
backend-specific handles.

## Geometry and projection

Use the canonical golden-ratio coordinates, normalized once:

```text
(0, +/-1, +/-phi)
(+/-1, +/-phi, 0)
(+/-phi, 0, +/-1)
```

The implementation keeps the face list as the topology authority and derives or
validates the 30 unique undirected edges from it. This avoids an attractive but
incorrect hand-authored line list with missing or duplicated edges.

Per loading frame:

1. Build a slow compound rotation around two non-parallel axes. Avoid rotation
   around only the screen normal, which reads as a flat spinning logo.
2. Transform the 12 vertices in model/view space.
3. Apply a bounded perspective projection with a fixed positive camera distance;
   ED4 never moves the object through the near plane.
4. Convert normalized projected coordinates into the visual rail around its
   center and scale by the rail's smaller dimension.
5. Classify faces by signed projected area or view-space normal, then classify
   each edge from its adjacent faces.
6. Draw rear-only edges first with low alpha and thin strokes; draw silhouette
   and front-facing edges afterward with brighter, thicker strokes.

Depth sorting every edge is unnecessary for a transparent wireframe, but the
front/rear distinction is required: 30 equally bright lines produce a flat,
noisy symbol rather than a readable solid form.

## Composition and responsive behavior

The existing loading card remains the information authority. ED4 computes a
visual rail from the full loading window:

```text
screen left + outer margin ... card left - gutter
```

The icosahedron is centered in that rail rather than changing the card's
telemetry layout. This directly consumes the unused left space and avoids
competing with the movable Startup Profiler on the right side.

The rail has a minimum usable width and height. When it falls below that
threshold, the visualization is omitted; it is not squeezed over text, moved
behind the card, or allowed to expand the loading window. The draw list uses
`PushClipRect`/`PopClipRect` for the rail so thick lines and later displacement
effects cannot invade the card.

Suggested initial presentation:

- diameter: 55–75% of the smaller rail dimension, with a fixed upper bound;
- rotation: one slow primary revolution with a smaller secondary-axis motion;
- rear edges: dim blue/cyan at roughly one quarter of front-edge alpha;
- front/silhouette edges: existing loading accent cyan;
- optional center reticle and sparse orbit ticks only if the mesh remains the
  dominant shape after visual inspection;
- no labels that duplicate startup telemetry.

All layout constants remain local named constants until a second loading theme
or user-facing setting creates a real configuration consumer.

## State and future fault-effect seam

Normal startup uses continuous time and the existing accent palette. `Ready`
may briefly use the existing green status color, while `Closing` remains muted.
The current `failed` state may switch the base wireframe to the existing fault
red, but ED4 does not add an elaborate glitch sequence.

The implementation should keep geometry projection separate from style and
line emission so a later stage can add bounded, deterministic effects such as:

- duplicated red/cyan edge ghosts;
- short horizontal offsets applied to selected screen-space bands;
- intermittent missing segments and scan lines;
- a seeded fault pulse that does not change every call within one frame.

Those effects can still use draw-list geometry. Move to an offscreen target only
when the accepted design requires sampling previously rendered pixels or
shader-only processing. If that happens, the later plan must name the owner,
creation point, resize policy, transition/synchronization path, ImGui sampling
bridge, and availability during early startup for both backends.

## Implementation stages

### ED4.1 — Projected wireframe model

- Add the canonical vertices/faces and derive or validate exactly 30 unique edges.
- Implement deterministic rotation, perspective projection, and front/rear edge
  classification without depending on Render or Graphics.
- Cover topology count, finite projected coordinates, rotation repeatability,
  bounds, and front/rear classification with focused tests.

### ED4.2 — Loading-screen composition

- Compute the left visual rail from the full viewport and current card bounds.
- Submit rear and front edge layers through the loading window's `ImDrawList`.
- Clip all decoration to the rail and suppress it below the minimum usable size.
- Derive normal/fault/closing colors from the existing loading view model without
  changing the Runtime snapshot contract.

### ED4.3 — Runtime visual acceptance

- Capture an active-loading frame on Vulkan and OpenGL with matched window size.
- Capture or inspect a narrow-window case proving the visual is suppressed rather
  than overlapping the card.
- Inspect a failed-state frame for readable diagnostic text and bounded fault
  coloring.
- Confirm loading-to-workspace promotion destroys the decoration with the loading
  tree and creates no persistent resource or teardown work.

## Acceptance criteria

- The loading screen shows one recognizable rotating wireframe icosahedron in
  the available left rail during active loading.
- The topology has 12 vertices, 20 faces, and 30 unique edges.
- Rear edges are visually subordinate to front/silhouette edges.
- The animation is frame-rate independent and contains no unbounded accumulated
  angle or per-frame heap growth.
- The visual never overlaps the loading card, footer, or Startup Profiler-owned
  region at the accepted reference size.
- Narrow windows omit the visual cleanly while preserving all loading telemetry.
- Loading and failure diagnostics remain legible.
- `engine/runtime/render` and `engine/runtime/graphics` are unchanged; no render
  target, sampled texture, backend handle, or scene-ready service is introduced.
- Debug Vulkan and OpenGL loading captures show equivalent placement and style.

## Validation

Implementation requires Level 3 per the
[validation matrix](../../validation_matrix.md), because this changes Editor
startup presentation behavior.

Minimum validation:

```powershell
.\tools\kp.ps1 validate engine/editor/ui/component/editor_loading_component.cpp
.\tools\kp.ps1 test -l editor
```

Then rebuild Debug and use the checked-in deterministic startup gate/fixture to
capture active-loading frames on both APIs. Launch the visual application
outside the sandbox, keep Vulkan validation enabled, and store captures below
`save/screenshots/validation/`. Record API, validation state, viewport extent,
fixture, working-tree state, and whether the visualization was active or
responsively suppressed.

## Risks

- **The visual is too busy.** Equal-brightness rear edges are the likely cause;
  keep rear edges thin and dim before removing perspective or topology.
- **The visual disappears too often.** Tune the minimum rail size from reference
  captures, but never solve it by drawing beneath the information card.
- **Aliasing differs between backends.** ImGui line tessellation and fractional
  coordinates can expose small differences. Prefer stable pixel-aligned centers
  and judge equivalence rather than pixel identity.
- **The helper becomes a miniature renderer.** Stop at projection and line
  styling. Lighting, GPU buffers, shader effects, and general scene primitives
  require a new consumer and a separate ownership decision.
- **Fault animation harms diagnostics.** Failure text is the priority. Later
  glitch effects must stay clipped, bounded in displacement and frequency, and
  must not flash the full viewport.
