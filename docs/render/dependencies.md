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
| Editor → Render | Editor consumes narrow presentation/capture views; Render never depends on Editor. |

Forbidden edges:

- Graphics/RHI must not load assets, compile shaders, or depend on Render.
- Runtime Render code must not depend on Editor UI/ImGui.
- Common Render/RHI headers must not expose Vulkan or OpenGL implementation
  types.
- Gameplay must not depend on Graphics or private RenderWorld/MeshProxy types.
