# Editor documentation and implementation guide

Follow the root [agent contract](../../AGENTS.md) and
[validation matrix](../validation_matrix.md).

- [PLANS.md](PLANS.md) maps architecture and proposed extensions;
  [editor_module.md](editor_module.md) describes existing editor behavior.
- [TODO.md](TODO.md) owns stage status and acceptance. A design document does
  not mean that the corresponding feature is implemented.
- Components own UI state and submit presentation data. They never own GPU
  objects or access native graphics contexts.
- `EditorUILib` owns ImGui integration and editor presentation adapters. Render
  owns scene rendering and the frame schedule; Graphics owns physical GPU
  objects, synchronization, submission, and presentation.
- Extend the existing editor-composite seam explicitly when editor presentation
  needs GPU work. Keep effect policy and ImGui types out of common RHI contracts.
- Preserve loading-safe presentation before a scene renderer exists. Preserve
  the existing Runtime/Editor dependency boundary and all third-party sources.
- UI rendering changes require visible Vulkan/OpenGL evidence, including
  `engine_window` captures. Use the approved external-desktop launch path;
  compilation alone does not validate visual behavior or lifetime.

Create an execution spec before starting a substantial ED stage implementation;
record actual validation and limitations in its dated journal.
