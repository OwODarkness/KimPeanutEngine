# Render Dependencies

```text
Gameplay → Render → Graphics/RHI → OpenGL / Vulkan
             ↘ Asset / Resource processing
```

| Edge | Rule |
|---|---|
| Render → Asset/Resource | Render requests asset identity and processed artifacts through their public services. |
| Render → Graphics | Render creates common descriptions and records common work. |
| Gameplay → Render | Gameplay publishes value-only source descriptors through the sink. |
| Runtime → Render | Runtime owns startup wiring and the frame/thread boundary. |
| Editor → Render | Editor consumes source-independent capture, metrics, target-view, extent, and terminal-pass capabilities; Render never depends on Editor. |
| Editor → Graphics bridge | API-specific EditorUILib code consumes only its matching typed presentation bridge; the common bridge exposes no native API type. |

Forbidden edges:

- Graphics/RHI must not load assets, compile shaders, or depend on Render.
- Runtime Render code must not depend on Editor UI/ImGui.
- Graphics common code must not depend on ImGui; native UI initialization and
  recording stay in the API-specific Editor/Graphics bridge pair.
- Common Render/RHI headers must not expose Vulkan or OpenGL implementation
  types.
- `RenderSystem` must not expose the backend's general `GraphicsContext` or a
  Render-owned target/attachment handle to Editor.
- Gameplay must not depend on Graphics or private RenderWorld/MeshProxy types.
