# Editor architecture and design map

The Editor hosts tools and owns UI presentation. Runtime owns simulation,
Render owns scene policy and frame scheduling, and Graphics owns physical GPU
objects and API execution. Components emit presentation data through
`EditorUI`; native integration stays in `EditorUILib` adapters.

Existing behavior, build targets, threading, and known boundary debt are in
[editor_module.md](editor_module.md). The current acceptance ledger is
[TODO.md](TODO.md); working rules are in [AGENTS.md](AGENTS.md).

## Stage designs

| Stage | Design boundary |
|---|---|
| [ED1](.plan/ED1.md) | Tool-row tabs and shared visibility |
| [ED2](.plan/ED2.md) | Layout regions, splitters, and persistence |
| [ED3](.plan/ED3.md) | Magnetic panel placement |
| [ED4](.plan/ED4.md) | Loading-safe CPU-projected wireframe decoration |
| [ED5](.plan/ED5.md) | Selective UI bloom through editor-owned presentation and common RHI commands |

## UI bloom extension

[ED5](.plan/ED5.md) proposes public ImGui draw-list markers and an editor-owned
RHI renderer. Editor owns glow semantics, region policy, draw adaptation, and
logical resource lifetimes. Graphics allocates and retires the physical
objects. Render retains the scene graph and chooses when the editor composite
runs. This explicitly extends the existing editor-presentation exception;
it does not transfer scene rendering into Editor.

The intended data flow is component emission tags → immutable UI frame packet
→ bounded emission/blur passes → ordered UI composition → presentation.
ImGui core, widget behavior, vertex layout, and vendored sources stay intact.
The feature is proposed, with no runtime or performance acceptance yet.
