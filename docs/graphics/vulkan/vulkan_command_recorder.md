# Vulkan Command Recorder

Location: `engine/runtime/graphics/backend/vulkan/vulkan_command_recorder.*`

`VulkanCommandRecorder` is the Vulkan-private implementation of the common
`graphics::CommandRecorder` vocabulary. It translates render intent into
`vkCmd*` calls; it is not a scene renderer, resource owner, frame scheduler, or
render graph.

## Position in the frame

```text
RenderSystem / RenderScene
    | common handles + CommandRecorder calls
    v
VulkanCommandRecorder
    | vkCmd* calls into the active command buffer
    v
VulkanFrameContext / VulkanBackend
    | submit + present
    v
GPU
```

`VulkanBackend` creates the recorder only after `vkBeginCommandBuffer` succeeds
and destroys it in `EndFrame` after closing the render-target bracket, before
editor composition and submission. A caller may obtain `CommandRecorder*` only
while recording is active; the pointer becomes invalid when `EndFrame` starts
closing the frame and must not be retained.

## Current responsibility

The recorder owns command encoding and small recording-local state:

- `BeginRenderTarget` / `EndRenderTarget`: dynamic rendering, clears,
  attachment transitions, and default viewport/scissor setup.
- `BindPipeline`, `BindMesh`, and `BindResourceBindings`.
- `SetViewport`, `SetScissor`, and `DrawIndexed`.
- The bound mesh's default index count used when a draw requests count zero.

The recorder borrows the active `VkCommandBuffer`, resource managers, and
`VulkanRenderTargetManager`. It never outlives them and never frees their
resources.

## Explicit non-responsibilities

| Responsibility | Owner |
|---|---|
| Device, queues, acquire, submit, present, fences | `VulkanBackend` / `VulkanFrameContext` |
| Buffers, images, descriptor sets, pipelines | Their Vulkan managers |
| `VkDeviceMemory`, mapping, suballocation | `VulkanMemoryManager` |
| Scene content, material policy, pipeline-cache policy | Render module |
| Editor ImGui composition | Temporary Vulkan-backend seam; moves in roadmap 6.4 |

## Future growth

Keep this type an encoder. New operations belong here only after a common RHI
caller needs them and their API-neutral description exists:

- non-indexed and instanced draw variants;
- push constants;
- compute dispatch;
- resource barriers required by a real multi-pass caller;
- GPU debug labels, timestamps, and profiling scopes.

A future render graph chooses pass order, resource dependencies, and barriers
at the render level. The recorder only emits the already-decided operations for
one active command buffer.

## Render-target boundary

`VulkanRenderTargetManager` owns attachment textures, layout state, compatible
preview views, swapchain-sized attachment recreation, and the dynamic-rendering
begin/end transitions. The recorder only selects a `RenderTargetHandle` and
asks that service to encode the target bracket.
