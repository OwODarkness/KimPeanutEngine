# ED6.2 — Neutral Editor UI and backend execution scope

- Status: implementation authorized; execution in progress
- Date: 2026-11-10
- Parent: [ED6 roadmap](../../docs/editor/TODO.md#ed6--reusable-ui-and-editor-reconstruction)
- Canonical stage design: [ED6.2](../../docs/editor/.plan/ED6.2.md)
- Parent execution scope: [Editor reconstruction](editor-ui-reconstruction.md)
- Design review: [ED6.2](../../docs/editor/.review/ED6.2.md)
- Evidence: [planning journal](../journal/2026-11-10-editor-ui-ed6-2.md)

## Objective

Establish a reusable Editor UI API that exposes no ImGui types/headers/context;
implement ImGui behind its private backend boundary; separate UIWidgets,
UIPresentation and backend targets; prove the boundary with a recording backend,
a real Settings property-fields view and an independently linked UI application.

Preserve a path toward AI Native through stable identity, semantic descriptors,
typed results and optional internal frame observations with actual layout
provenance. Domain owners remain authoritative. This stage does not define an
agent protocol, JSON UI schema, runtime-generated layout system or second
production GUI backend.

## Authorization and current state

The user's 2026-11-10 instruction, “ED6.2 detailed plan 根据此方案执行,”
supersedes the planning-only authorization recorded when this scope was drafted.
It authorizes source and CMake changes within the canonical ED6.2 boundaries.
Preserve existing dirty Settings, Log and ED6.1 implementation. Do not claim
acceptance until G1–G8 have execution evidence.

## Implementation boundaries

The design orders ED6.2.0–ED6.2.6: contract freeze, neutral core/recording,
ImGui adapter, presentation lifecycle, real feature pilot, host delegation,
and final acceptance. Its v1 capability list, ownership table and legacy
allowlist are the implementation boundaries; API expansion requires a current
consumer and explicit contract review.

- Feature code uses UiContext and Editor-owned values, not toolkit escape hatches.
- RuntimeLib/EditorLib/SceneEditor are absent from new neutral/backend target
  dependencies; the old aggregate remains a one-way migration consumer.
- Keep ED6.1 hidden update/close decisions and current domain revision/provider
  behavior. Existing snapshots, resource retirement and game/render lanes remain.
- Legacy callbacks are confined to compatibility and remain listed migration
  debt; new callers and the real neutral pilot cannot use that bypass.
- UI image tokens have explicit validity; views do not own GPU resources.
- Semantic observations are optional, bounded, immutable copies and distinguish
  actual versus simulated geometry. They are not another domain-state store.
- No source copies/imports from reference projects or modifications to third_party.

## Current G8 ImGui allowlist

Audit method: `rg -l 'imgui\.h|ImGui::|ImVec[234]|ImU32|ImFont|ImTextureID'
engine/editor -g '*.{h,hpp,cpp}'`. Entries below describe the current working
tree on 2026-11-11. New files do not inherit this allowance; every new match
must either move behind the backend boundary or be added with an owner and
ED6 cleanup stage.

### Private production backend and platform adapters

These are the intended ImGui implementation boundary for the feature API:

- `engine/editor/ui/api/ui_imgui_backend.cpp`
- `engine/editor/ui/api/ui_imgui_inline_backend_internal.h`
- `engine/editor/platform/editor_imgui_glfw_wsi.cpp`
- `engine/editor/platform/editor_imgui_opengl_renderer.cpp`
- `engine/editor/platform/editor_imgui_opengl_renderer.h`
- `engine/editor/platform/editor_imgui_renderer.h`
- `engine/editor/platform/editor_imgui_rhi_renderer.cpp`
- `engine/editor/platform/editor_imgui_rhi_renderer.h`
- `engine/editor/platform/editor_imgui_vulkan_renderer.cpp`
- `engine/editor/platform/editor_imgui_vulkan_renderer.h`

### Editor compatibility code pending later ED6 stages

These are retained inside the old `EditorUILib` aggregate and remain explicit
migration debt. Their current target owner is `EditorUILib`; the cleanup owner
is ED6.5/ED6.7. They are not allowed for neutral feature or UI API targets.

- Actor: `engine/editor/actor/editor_actor_inspector_component.cpp`,
  `engine/editor/actor/editor_world_outliner_component.cpp`.
- Asset: `engine/editor/asset/editor_asset_browser_component.cpp`,
  `engine/editor/asset/editor_asset_reference_component.cpp`.
- Gizmo: `engine/editor/gizmo/transform_gizmo.cpp`,
  `engine/editor/gizmo/transform_gizmo.h`.
- Profile: `engine/editor/profile/editor_profile_bar.cpp`.
- Component compatibility (`engine/editor/ui/component/`):
  `editor_camera_settings_component.cpp`, `editor_console_component.cpp`,
  `editor_console_component.h`,
  `editor_debug_viewer_component.cpp`,
  `editor_gpu_profiler_component.cpp`, `editor_layout_model.h`,
  `editor_loading_component.cpp`, `editor_menubar_component.cpp`,
  `editor_splitter_handles.cpp`, `editor_startup_profiler_component.cpp`,
  `editor_tool_row_component.cpp`, `editor_tool_row_model.h`,
  `editor_ui_component.h`,
  `editor_viewport_component.cpp`, `editor_viewport_component.h`,
  `editor_window_component.cpp`.
- Legacy host/theme/effect bridge (`engine/editor/ui/`): `editor_theme.cpp`,
  `editor_theme.h`, `editor_ui.cpp`, `editor_ui.h`, `editor_ui_frame_packet.h`,
  `editor_ui_glow.h`.

This is a measured allowlist, not a completion claim for ED6.5/ED6.7. The final
G8 audit must rerun the scan, compare paths with this list, inspect target
dependencies, and report any newly introduced entry or removed migration debt.

## Acceptance and validation

Canonical design gates G1–G8 cover compile isolation, independent links,
behavior/precision/scope semantics, AI-adjacent observations, lifecycle failures,
image/Graphics correctness, enabled-host matrices, dependency/performance review.
All must have actual evidence before accepting ED6.2. A recording-only test does
not prove production pixels, another GUI implementation or GPU safety.

Implementation changes shared APIs and CMake boundaries: Level 4 is required
with targeted inner-loop tests, then visible Debug Vulkan validation and OpenGL
checks through the external Default-desktop path, plus matched rebuilt
RelWithDebInfo timing. The ED6.0 instantaneous sample is not a matched p95
baseline; establish repeatable samples before source changes.

The original planning authorization was superseded by the user's explicit
execution request. The implementation has begun, but the required Level 4
acceptance matrix remains open; see the execution journal for actual results and
unverified gates.

## Remaining work

The full feature/workspace toolkit migration remains ED6.5 and final enforcement
ED6.7; Settings feature ownership acceptance remains ED6.4. Existing ED5/SET3
acceptance and known failures stay independent. Exact C++ declarations are
frozen at ED6.2.0 within the documented semantics, before implementation grows.
