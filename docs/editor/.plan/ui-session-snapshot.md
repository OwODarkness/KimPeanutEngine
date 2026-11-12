# Agent-readable GUI session snapshots

This proposal adds a read-only `ui.snapshot` command for a running GUI session.
The result describes the session's layout, hierarchy, styles and semantics
from one completed UI frame. The first production consumer is Audio Player.
SceneEditor can attach the same command provider as its surfaces migrate to
the neutral UI and Workspace contracts. [ED6.2 observation design](ED6.2.md#5-minimum-foundation-for-later-ai-native-work)

## Ownership and data sources

| Owner | Snapshot contribution |
|---|---|
| `UiSession` and `UiContext` | Session/frame identity, semantic nodes and ordered parent scopes, typed values, supported actions, theme references and the frame-local collector. `UiSession::Query()` returns a pinned `UiSemanticView` for tree, element and style queries over one completed frame. |
| Private UI backend | Actual bounds and clip, interaction state, viewport metadata, resolved neutral style fields and window stacking information where available. Correlate observations with the IDs assigned by `UiContext`. |
| `EditorWorkspace` | Registered panels, split tree with axes and resolved ratios, region bounds, ordered tabs and active member, floating placements and panel visibility. Project the same state used for that frame's layout. |
| Feature UI | Labels, units, descriptions and summaries for custom widgets. The audio player supplies playback meaning and values through its view. |
| `EditorUiHost` and application composition | Request an observed frame, publish its immutable snapshot after successful submission, and register a session-scoped command provider through the existing command registry. |

`UiFrameResult` currently carries `UiNodeObservation` records, and
`EditorUiHost` retains the last complete frame's vector. Reuse these types
and the existing observation flag. Move semantic parent tracking into the
neutral scope path so recording and ImGui agree. The current backend frequently
assigns the enclosing window as parent, and text, tab and table operations need
explicit coverage. Extend those operations with stable IDs where a semantic
node is needed. [Session lifecycle](../../../engine/editor/ui/api/ui_session.cpp#L64), [observation fields](../../../engine/editor/ui/api/editor_ui/ui_types.h#L215), [backend collection](../../../engine/editor/ui/api/ui_imgui_backend.cpp#L491), [host publication today](../../../engine/editor/ui/host/editor_ui_host.cpp#L161)

Use the existing `UiTheme` values and text/font roles as the neutral style
vocabulary. Intern repeated style values in a per-frame table. Capture the
theme at each node's own scope, with effective properties reported where the
backend can supply them. A declaration can describe a theme even when that
frame has no backend measurement. Workspace contributes the geometry and
placement it actually uses. It already owns panel state, active membership
and resolved split and region bounds. [Theme types](../../../engine/editor/ui/api/editor_ui/ui_theme.h#L8), [workspace model](../../../engine/editor/workspace/editor_workspace_model.h#L78)

## Snapshot contract

| Part | Contents |
|---|---|
| Header | `schema_version`, `snapshot_id`, `session_id`, `frame`, host/mode, viewport logical origin and extent, work area, framebuffer scale and coverage. |
| `nodes` | Flat ordered array with `id` and `parent_id`. The session root has a null parent. Roles cover window, group, tab list, tab, table, row, heading, text and controls. Overlays can refer to their owner by an additional relationship while retaining their own window geometry. |
| Node semantics | Label, description, typed value, unit, enabled/selected/focused/expanded state where applicable, disabled reason, and action descriptors. A Canvas, plot or custom-drawn control supplies a stable role, label and optional domain summary. |
| Node geometry | Bounds are `[x,y,width,height]` in session logical coordinates. Subtract the viewport origin before applying framebuffer scale for viewport-local pixels. Record actual bounds, optional clipped bounds, geometry provenance and optional stacking/occlusion metadata separately. An unmeasured node has null geometry. Clipping describes the clip region. Occlusion describes coverage by a higher surface. |
| `workspaces` | Split/region tree with stable IDs, axis, requested and resolved ratio, child references and bounds. Include tab order, active tab, closed and inactive panel registrations, floating rectangles and links to corresponding semantic nodes. |
| `styles` | Interned records of supported neutral properties such as foreground/background, font role and logical size, padding, spacing, rounding and border. Use style references on nodes. Distinguish declared properties from backend-resolved properties and omit unsupported fields. |
| `coverage` | Distinguish complete, partial and unavailable coverage. Identify legacy surfaces, bounded text, virtualized collections and omitted data with reasons and known counts. Virtualized lists report total items and materialized range. Inactive tabs expose the panel node and its state. Their controls become observable when the host submits them. |

Scope IDs by session and stable semantic paths within that session. Use feature
keys for repeated domain items. An item's ID survives a change of label,
selection or panel placement. The parent records the frame's actual hierarchy,
so moving a panel changes its parent while preserving its ID. Encode opaque IDs,
frame counters and 64-bit integer values as strings at the JSON boundary to
preserve precision in clients. UI-facing data follows each view's disclosure
policy. Redact sensitive values at capture, before any copy leaves the UI lane.
Custom rendering contributes semantic summaries and geometry through the
neutral API. Draw commands, image bytes and GPU handles stay with presentation.

## Command and lifecycle

1. `ui.sessions` lists attached GUI sessions and their host/mode. `ui.snapshot`
   accepts an optional `session_id` and `view`. `view` is `"full"` by default
   or `"overview"`. With one active session it selects that
   session by default. With multiple sessions the caller chooses explicitly
   from the list. A headless process can report an empty list.
2. The registry dispatches capture on the game lane using its existing deferred
   request contract. The provider queues a one-shot observation request for
   the selected presentation host. At the next eligible frame start the host
   latches the request. Requests for the same session can share that capture.
   Ordinary frames keep their current observation cost when no consumer asks.
3. The host publishes a single immutable snapshot only after UI scopes close
   successfully and presentation submission returns successfully. The
   workspace contribution uses the placement rendered in that frame. If
   placement intents commit for the next frame, they appear in that next
   snapshot. Abort or submit failure discards the in-progress capture. Complete
   the command with the captured frame, a bounded timeout or session-close
   result through the registry's completion sink. Wire cancellation to remove
   the individual waiter from the mailbox. The current registry cancels the
   command result, so provider cleanup needs a cancellation notification or a
   request-liveness check. Other waiters still receive the frame.
4. The agent polls the existing `request_id` and gets a nested object at
   `data.snapshot`. Build JSON from the immutable data away from the UI lane.
   The current `CommandData` supports scalar values only. Add an optional
   result-only structured payload with a field name and owned UTF-8 JSON. The
   endpoint validates it as an object and embeds it under that field in
   `data`. Existing scalar arguments and result fields keep their contract.
   Reject a conflicting field name or invalid object as a command error.
5. Bound outstanding requests, captured text and response bytes. For a
   snapshot that fits the capture budget but exceeds a reply, return an opaque
   `next_cursor`. Use `ui.snapshot` with `snapshot_id` and `cursor` to retrieve
   the next page from the same frozen frame. Page records are emitted in a
   defined order across layout, styles and nodes, so even a large style or
   layout table can span pages. Clients resolve references after assembly.
   Bound snapshot retention and report expired cursors explicitly. Capture
   limits set partial coverage with omission reasons. The reply limit counts
   serialized bytes including the command envelope.

The application owns command registration and provider teardown. Use the
same host attachment seam as existing host commands. The provider resolves a
session to a request mailbox and accesses only completed owned copies. The
transport thread never accesses `UiContext`, a Workspace model, or an ImGui
context. Unregister on detach and complete waiters before releasing the host.
Use the command registry's existing authorization. Action descriptors in a
snapshot tell the agent what a control means and what inputs it accepts.
Executing those actions is a separate contract. [Command result and completion sink](../../../engine/runtime/command/command_types.h#L121), [deferred command dispatch](../../../engine/runtime/command/command_registry.cpp#L341), [Agent endpoint](../../../engine/runtime/command/command_agent_endpoint.cpp#L47), [existing capture provider](../../../engine/runtime/screenshot/screenshot_command_provider.cpp#L138)

## Example

```json
{"op":"execute","command":"ui.snapshot","arguments":{"session_id":"3"}}
```

Example `data.snapshot` after polling the request, using a reduced audio
workspace. The layout tree refers to panel nodes by ID.

```json
{
  "schema_version": 1,
  "snapshot_id": "3/1842",
  "session_id": "3",
  "frame": "1842",
  "host": "audio-player",
  "viewport": {"origin": [0, 0], "extent": [1280, 720],
               "work_area": [0, 0, 1280, 720], "framebuffer_scale": [1, 1]},
  "coverage": {"status": "complete"},
  "workspaces": [
    {"id": "audio", "root": "columns", "regions": [
      {"id": "preview", "bounds": [0, 0, 960, 720], "tabs": ["now_playing"], "active_tab": "now_playing"},
      {"id": "info", "bounds": [960, 0, 320, 720], "tabs": [], "active_tab": null}
    ], "splits": [
      {"id": "columns", "axis": "x", "children": ["preview", "info"],
       "bounds": [0, 0, 1280, 720], "requested_ratio": 0.75, "resolved_ratio": 0.75}
    ], "floating": []}
  ],
  "styles": [{"id": "s1", "declared": {"font_role": "code"}, "resolved": {"font_size": 14}}],
  "nodes": [
    {"id": "root", "parent_id": null, "role": "session"},
    {"id": "now_playing", "parent_id": "root", "role": "panel", "label": "Now Playing"},
    {"id": "seek", "parent_id": "now_playing", "role": "slider", "label": "Playback position",
     "value": {"type": "float", "value": 159.1, "unit": "s"},
     "range": {"min": 0, "max": 304.9}, "style_id": "s1",
     "geometry": {"bounds": [24, 528, 920, 18], "provenance": "actual_backend"},
     "state": {"enabled": true}, "actions": ["edit", "commit"]}
  ]
}
```

### GUI Semantic Interface

The query contract is feature-neutral. `UiSession` owns the last completed
frame and exposes three read-only queries:

- `GetUITree()` returns stable element IDs, parent/child relationships, roles,
  labels, bounds, geometry provenance and basic state.
- `GetElementInfo(id)` returns the element's full semantic observation,
  including typed value, enabled/selected/focused state, actions, ranges,
  table/chart metadata and child IDs.
- `GetElementStyle(id)` resolves the element's style reference to the declared
  neutral `UiTheme`. It reports whether backend-resolved properties are
  available instead of implying that declared values are final pixels.

The queries operate on the latest completed frame, never an in-progress UI
build or backend context. `UiSession::Query()` returns a pinned immutable view;
tree, element and style queries through that view always refer to one frame and
remain valid while the view is retained, including across later submissions.
Missing elements and unavailable styles return no result. A hierarchy is generic (`title`, `role`, `id`, `bounds`, `state`,
`children`); application-specific concepts remain semantic data on the
relevant nodes. Audio Player is a validation consumer, not the schema owner.

`ui.snapshot` can serialize the same interface for remote agents. A full view
includes geometry, style references and bounded chart samples; an overview
may reduce detail but must preserve generic roles, IDs, parent relationships,
bounds and state. Both come from one successfully submitted UI frame, and
paging continues the selected view by `snapshot_id` and `cursor`.

```json
{"op":"execute","command":"ui.snapshot","arguments":{"view":"overview"}}
```

## Geometry and state definitions

Backend measured rectangles use ImGui main-viewport logical coordinates, the
same coordinate basis as `ImGuiViewport::WorkPos`. Bounds are
`[x, y, width, height]`. Convert to viewport-local logical coordinates by
subtracting `viewport.origin` from x and y; convert those local coordinates to
framebuffer pixels by multiplying each axis by `framebuffer_scale`. Workspace
regions and split rectangles use this same logical basis.

`clipped_bounds` is the intersection of an element's bounds, the active ImGui
draw-list clip rectangle and the viewport work area. `clipped` is true when
that intersection does not cover the full measured bounds. `visible` means the
element was submitted in this frame and has a non-empty visible intersection;
it does not claim that a higher window does not cover it. Missing optional
state is serialized as `null`, never as `false`.

`value` is the intrinsic value for a role and is omitted when the role has no
intrinsic value (for example, a Button). `selected` means role-specific
selection for Selectable, Tab, Panel or Row; checkbox state is in `value`.
`active` means the backend reports an ongoing interaction, while `focused`
means keyboard or navigation focus. These optional states are null when the
role or backend cannot report them. `enabled` reports whether interaction is
allowed.

## Implementation and acceptance

1. Extend the neutral observer and compare production and recording output for
   the same semantic scopes. Cover nested tabs/tables, stable IDs, repeated
   labels, theme overrides, hidden versus clipped geometry, and virtualized rows.
2. Add generic workspace layout projection and prove nested splits, tabs,
   floating panels, bounds and styles remain associated with semantic nodes.
   Use Audio Player and SceneEditor as independent consumers. Validate bounds
   against observed frames in Debug Vulkan and OpenGL, including a scaled viewport.
3. Register the read-only commands and verify JSON types, transport and
   polling, no GUI, multiple sessions, teardown during capture, failed frames,
   limits and continuation consistency. Include a large playlist and confirm
   that normal unobserved frames keep their existing cost. Record SceneEditor
   legacy regions as partial coverage until their neutral projections exist.

The protocol borrows the separation of hierarchy, layout and computed style
from Chromium's snapshot schema, and stable tree/node identity with semantic
properties from AccessKit. These inform the neutral snapshot fields.
[Chromium DOMSnapshot protocol source](https://raw.githubusercontent.com/ChromeDevTools/devtools-protocol/master/pdl/domains/DOMSnapshot.pdl),
[AccessKit node and tree source](https://docs.rs/accesskit/latest/src/accesskit/lib.rs.html)
