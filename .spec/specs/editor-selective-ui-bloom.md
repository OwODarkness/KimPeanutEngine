# Editor selective UI bloom

- Status: active
- Owner: Codex
- Parent TODO: [ED5](../../docs/editor/TODO.md)
- Architecture: [ED5 design](../../docs/editor/.plan/ED5.md)

## Objective

Implement explicit, bounded bloom for selected Editor UI geometry while
preserving ordinary ImGui input, clipping, painter order, and loading
presentation. The implementation must use the common Graphics contract for
GPU work and keep effect policy in Editor.

## Current state

The editor retains official ImGui renderers as a readable fallback. A common
adapter now converts copied ImGui draw data into Graphics commands, uploads the
font atlas, tracks borrowed render-target sampling metadata, and records a
linear color canvas plus bounded emission/filter/composition passes. Loading
wire/status and selected World Outliner rows emit copied public ImGui markers.
The Tool-menu disable switch gates both accents. Vulkan has a readable runtime
capture; OpenGL still shows a UI color mismatch and needs correction before
cross-backend acceptance.

## Scope and non-goals

Implement the ED5.1–ED5.4 contracts and bounded acceptance slice: copied
emission markers, a common frame-scoped presentation capability, ImGui packet
conversion, RHI source/filter/composition passes, selected/loading accents,
global disable and fallback. Do not alter Dear ImGui or vendored backends.
Scene bloom, multi-viewport rendering, physical UI lighting, and HDR display
output remain out of scope.

## Invariants

- ImGui remains render-thread-only; borrowed frame data does not escape a frame.
- Editor owns UI policy and logical lifetimes; Render owns frame ordering;
  Graphics owns physical allocations, transitions, and retirement.
- Graphics and Render common contracts expose no backend-native API types.
- Loading presentation remains available before deferred scene promotion.
- Effect preflight/filter failure preserves readable base UI.

## Stages

1. Freeze texture, callback, color, marker, and frame-capability contracts.
2. Implement common RHI presentation with zero-emission parity and safe reuse.
3. Add one bounded emission region and half-resolution ordered filtering.
4. Integrate loading and selected accents, budgets, fallback, cross-backend
   runtime evidence, and performance evidence.

## Acceptance criteria

- [ ] Common UI adapter preserves input, clipping, texture origin, geometry
  offsets, and zero-emission appearance on Vulkan and OpenGL.
- [ ] Only copied, explicitly marked source geometry emits; clipped source
  does not emit, later UI occludes the halo, and scene images remain excluded.
- [ ] One bounded region filters and composites in painter order; disable,
  unsupported formats, and budget/filter failure retain readable base UI.
- [ ] Loading wire and selected accent work before scene readiness.
- [ ] Resource reuse/retirement is safe across resize and shutdown.
- [ ] Debug Vulkan validation, OpenGL runtime, visual cases, and matched
  RelWithDebInfo measurements are recorded.

## Validation plan

Follow [the validation matrix](../../docs/validation_matrix.md). The shared
Graphics/Render presentation contracts require Level 4 and visible Vulkan and
OpenGL evidence. Do not launch the engine in the sandbox.

## Risks and open questions

- The adapter needs a borrowed common capability for backend resources and
  scoped recording that works before deferred renderer initialization.
- Registered texture metadata must identify font coverage, color texture
  transfer, and render-target UV origin without interpreting native handles.
- The temporary layered-accent prototype must be removed once the filtered
  source/composition path is live.
