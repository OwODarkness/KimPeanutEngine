# Render owner boundaries and agent reuse workflow

- Status: active.
- Owner: Render / repository agent workflow.
- Parent TODO: [Render roadmap](../../docs/render/TODO.md).
- Baseline: `f1f2734`; preserve the separate Scene3D startup repair.

## Objective and scope

Resolve the six post-R7.3 review findings: submitted-only PT progress, one
actual frame mode, owner-reported texture footprints, shared environment
ownership, camera-owned projection, and a lightweight RenderSystem header.
Add a shared ownership/reuse workflow for agents with a read-only evidence
command. Graph scheduling and physical binding composition remain in Render.

## Placement and reuse evidence

| Operation | Existing candidate / search | Placement decision |
| --- | --- | --- |
| Projection | `BuildWorldRay`, `ProjectScenePoint` | Extend RenderCamera; facade delegates. |
| Texture sizes | `GetTextureMipByteCount`, allocation-byte query | Reuse Core format sizing; RenderTarget reports color footprints, owners aggregate. |
| Environment | `EnvironmentBindingsOwner`, PT/deferred consumers | Move existing owner to frame composition; consumers borrow bindings. |
| Frame mode | requested/effective/active PT checks | Small pure mode contract shared by preparation, graph and commit. |
| PT history | `CommitPathTraceHistoryProgress`, RNG/adaptive state | Stage during graph finalization, publish only after submission. |
| Public settings | pass headers and RenderSystem include graph | Value-only AO/TAA headers; forward-declare renderer. |

## Invariants and acceptance

- [ ] Failed submission advances no PT history/RNG/adaptive state or raster history.
- [ ] Missing/over-capacity PT scene selects raster TAA/AO from actual mode.
- [ ] Required histories prepare before backend BeginFrame, with failure recovery.
- [ ] Texture accounting derives formats from target descriptions; disabled TAA
      still reports active raster guides/AO, unsupported allocation stays unknown.
- [ ] Environment bindings have one common owner; GPU ownership is unchanged.
- [ ] Projection supports perspective/orthographic cameras without renderer math.
- [ ] RenderSystem header does not include the concrete renderer/pass owners.
- [ ] Agent workflow requires search/placement/review evidence, permits justified
      local helpers, and has a usable read-only command including staged changes.

## Reference and validation

Inspected local gkNextEngine `4ba5b7cd106c282e7ed166ff853aeea87b392680`:
`src/Engine/Rendering/ViewCameraUboBuilder.hpp` separates camera operations from
renderer recording; `RenderView.hpp` names temporal state/invalidation separately
from resource ownership. Adopt focused operations and explicit lifetimes, not
its Vulkan types or broad mutable per-view state. No source copied.

Build Debug engine and affected tests, run Render/Graphics labels and broad
CTest after public-header changes. Run visible Debug Vulkan/OpenGL canonical
Sponza captures and mode/history lifecycle checks through Runtime commands.
Do not claim new performance acceptance without matched RelWithDebInfo runs.
Exercise the agent evidence command on tracked, staged and explicit paths.
