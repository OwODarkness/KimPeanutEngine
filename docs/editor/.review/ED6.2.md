# ED6.2 — Design-Risk Review

- Date: 2026-11-10
- Scope: detailed design and static source/target inspection; not implementation acceptance
- Status: design constraints recorded; implementation risks remain open
- Design: [ED6.2](../.plan/ED6.2.md) / [scope](../../../.spec/specs/editor-ui-ed6-2.md)
- Baseline: HEAD `90f6597668c74774fcbdd3e11ed57cd0cf9545d3` plus uncommitted
  Settings, Log and ED6.1 work; retain the limitations in [ED6.0](ED6.0.md)

## Risks and design disposition

| Risk | Local evidence | Design constraint | Verification / status |
|---|---|---|---|
| High: extracting only the renderer leaves features toolkit-bound | Public window extra_flags, renderer ImTextureID/ImVec2/LogColor, direct feature widget calls | UiContext with neutral descriptors/results; native mapping stays private | G1/G2; implementation open |
| High: new targets still reach Runtime/Scene through the aggregate | EditorUILib combines tools and links RuntimeLib, which depends on EditorLib | New targets cannot depend on the aggregate; real minimal-app and recording-only links | G1/G2/G8; implementation open |
| High: duplicate context, input or submission | InitializeViewer calls InitializePresentation, which creates context, loads configuration and constructs loading UI; five host families reuse it | One UiSession; old entry delegates; isolated legacy callbacks; application polls once | G5/G7; implementation open |
| High: image abstraction loses lifetime/GPU safety | RenderTargetView contains native fields and expires on resize; existing renderer caches bindings and uses WaitIdle paths | Frame/session generation tokens, backend resolution and preserved Graphics retirement | G6; implementation open |
| High: nominal AI preparation still exposes only draw geometry | Current frame packet converts ImDrawData/geometry/glow without domain semantics | Explicit UiNodeId/role/label/state/action and optional observer; actual/test geometry separated | G4; implementation open |
| Medium: a second retained/domain store grows into an agent framework | Public UI API and later agent schema can become conflated | Bounded immutable observations only; no JSON/endpoint/UI authoring system; domain model remains authoritative | G4/G8; implementation open |
| Medium: all-feature migration makes the stage unbounded | Baseline features use tables, plots, draw lists, fonts and input | Fixed v1; real Settings-fields subview pilot; remaining legacy inventory has later owners | G7/G8; implementation open |
| Medium: recording success is mistaken for layout/graphics acceptance | Scripted events do not execute native layout, IME or GPU work | Label simulated geometry; separately test actual bounds/captures and both graphics APIs | G3-G7; implementation open |
| Medium: old timing evidence cannot establish regression | ED6.0 has roughly 15 seconds of visible instantaneous rows, with camera not independently confirmed | Repeated matched baseline before coding; three 30-second windows; observation off/on and CPU/GPU scopes separated | G8; implementation open |

## Review conclusion

The stage boundary must include GUI operations, presentation lifecycle, a
private backend and a connected real consumer. Directory moves, CMake PRIVATE,
JSON generation or extracting Vulkan/OpenGL renderers cannot individually prove
that feature APIs are independent of ImGui. Neutral-consumer compile/link
evidence and production wiring are required.

AI Native preparation belongs in identity, semantic and result contracts now.
Production transport, automation authorization and bidirectional UI authoring
remain later designs. Existing domain command/edit paths remain the behavior
boundary.

## Evidence inspected

- EditorPanelContent/WindowVisibility and ED6.1 tab/window call paths.
- EditorUI InitializeViewer/InitializePresentation/RenderActiveTree and five
  standalone host families.
- Public renderer/WSI, frame packet, RHI renderer, Graphics presentation bridge
  and expiring image view.
- Real Settings scalar/enum helpers, source-target placement and model/request
  boundaries.
- Window/Graphics/Core/ImageIO dependencies; Core is a broad umbrella rather
  than a cost-free foundation-value target.
- Pinned AccessKit architecture/schema, ImGui Test Engine item information/hooks
  and RmlUi render interface. Sources and adopt/reject decisions are in the plan.

Only documentation validation and placement evidence were run. No build, test,
engine launch, new resource-lifetime proof, visual comparison or performance
measurement was performed. Close these risks with implementation evidence.
