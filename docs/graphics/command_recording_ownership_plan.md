# Command Recording Ownership Split

**Status:** complete (2026-08-29)  
**Prerequisite for:** [Deferred PBR renderer](../render/deferred_pbr/PLANS.md)
**Parent roadmap:** [Graphics next roadmap](TODO.md)

## Problem

RenderBackend is the global Graphics facade: it owns device/backend services, frame-slot lifecycle, acquire/submit/present, resize coordination, and GPU resource services. CommandRecorder is a short-lived per-frame encoder: it owns only recording-local state and converts already-decided Render commands to native API calls.

The implementation is asymmetric. VulkanBackend owns a per-frame VulkanCommandRecorder, creates it after the native command buffer begins, returns it through GetCommandRecorder, and destroys it before submit. OpenglBackend inherits both RenderBackend and CommandRecorder; GetCommandRecorder returns this. It therefore mixes frame scheduling with active target, bound pipeline, bound mesh, and default index-count state.

Immediate OpenGL calls do not remove encoder lifetime or ownership. This inheritance makes the backend a global mutable command object, differs from Vulkan lifetime behavior, and makes multi-pass attachment work harder to reason about.

## Decision

Keep RenderBackend as the Graphics global facade and frame scheduler, but prohibit every concrete backend from implementing CommandRecorder.

Each BeginFrame creates exactly one backend-private non-copyable recorder for its active frame. GetCommandRecorder returns a borrowed pointer only while the recorder is active. EndFrame closes any open target, destroys or invalidates the recorder, then submits and presents. A recorder is an encoder, not an owner of device, resources, fences, or scene policy.

    RenderSystem
      -> RenderBackend BeginFrame       (acquire / wait / begin recording interval)
      -> borrowed CommandRecorder       (pass-local encoder calls)
           -> VulkanCommandRecorder     (active Vk command buffer + private services)
           -> OpenglCommandRecorder     (current GL context + private services)
      -> RenderBackend EndFrame         (close / submit / present / advance slot)

This is a composition change, not a second global scheduler. Render pass scheduling, camera policy, and graph policy remain in Render.

## Required shape

### Common contract

The current CommandRecorder vocabulary stays the only Render-facing recording contract.

- A returned pointer is borrowed and invalid at the first step of EndFrame, cleanup, failed acquisition, or resize abort.
- A recorder cannot outlive its backend, native frame context, or referenced manager services.
- Recording calls never cause frame lifecycle operations.
- RenderBackend never implements or casts to CommandRecorder.
- Resource creation/destruction remains a RenderBackend service.

A later scoped FrameRecording token can replace the nullable pointer, but it is deliberately out of scope. Keeping the present call shape minimizes simultaneous Render API churn.

### Vulkan

Vulkan already follows the intended ownership direction. Preserve it: VulkanBackend creates VulkanCommandRecorder after beginning the native command buffer; the recorder borrows the active buffer and private pipeline, descriptor, buffer, mesh, target, and bindless services; VulkanBackend still records readback work, owns editor work, submits, presents, and advances the slot.

Audit failed BeginFrame paths so EndFrame never dereferences a recorder that was not constructed.

### OpenGL

Extract an OpenglCommandRecorder final class and remove CommandRecorder inheritance from OpenglBackend.

The recorder receives only active-recording dependencies: OpenGL target access, pipeline manager, mesh manager, descriptor/bindless service, and a frame-validity token. It owns the recording-local fields now on OpenglBackend:

- active render target;
- bound pipeline;
- bound mesh and default index count;
- framebuffer and framebuffer-sRGB state reset for the target bracket.

It must not receive OpenglBackend only to call public methods. If target access cannot be supplied narrowly, first extract a private target-recording service; do not recreate backend-as-recorder through a back pointer.

OpenglBackend creates and initializes this object after the GL frame/context is valid. EndFrame tells it to close an open target, destroys it, then performs readback collection/presentation and clears frame-active state.

## Stages

1. [x] Inventory OpenGL recording-local state and preserve the existing common recorder lifetime contract.
2. [x] Preserve Vulkan's existing short-lived recorder ownership without expanding public API.
3. [x] Introduce `OpenglCommandRecorder`; move recording methods/state; remove the inheritance.
4. [x] Validate Vulkan/OpenGL parity through the current RenderSystem, including resize, capture/readback, and shutdown.
5. [x] The attachment, shadow, and G-buffer plan may now rely on equivalent recorder ownership.

## Ownership boundary

| Concern | Owner |
|---|---|
| Context/window, acquire, submit, present, fences, resize | RenderBackend and private frame services |
| GPU resources and managers | RenderBackend composition root / private managers |
| Pass order, visibility, materials, draw lists | Render |
| One active frame command state and native calls | backend-private CommandRecorder |
| Frame UBO/descriptors | Render FrameContext backed by Graphics services |
| Native target states/transitions | backend-private target/recorder collaborators |

## Rejected alternatives

- Keep OpenglBackend as a CommandRecorder: rejected because the scheduler becomes a global mutable encoder.
- Let Render own native command buffers or recorder classes: rejected because it leaks API details.
- Reuse one global recorder across frames: rejected because stale state and frame-slot association become unsafe.
- Build a render graph first: rejected because it would sit above inconsistent encoder ownership.

## Reference evidence

- The local [Vulkan recorder design](vulkan/vulkan_command_recorder.md) defines the desired model: creation after native recording begins, borrowed manager services, destruction before submission.
- [Dawn Vulkan device implementation](https://github.com/google/dawn/blob/main/src/dawn/native/vulkan/DeviceVk.cpp) separates backend device implementation from command encoding objects. The WebGPU API is not imported; only the ownership distinction applies.
- [gkNextEngine frame submission](https://github.com/gameknife/gkNextEngine/blob/main/src/Engine/Rendering/FrameSubmission.cpp) waits frame-slot fences before reuse and submits after frame recording. It supports the lifetime rule, not a new public interface.
- The existing [Sakura reference](sakura_reference.md) distinguishes device/queue ownership from frame execution. KimPeanut retains its facade because private frame services already exist; only encoder composition changes.

## Acceptance criteria

- [x] Neither VulkanBackend nor OpenglBackend inherits CommandRecorder.
- [x] Each backend creates one recorder only for an active frame and returns none outside it.
- [x] Recorder-local targets and bindings cannot survive EndFrame or cleanup.
- [x] Render-facing command calls and resource-handle ownership are unchanged.
- [x] GraphicsSmoke passes on both APIs with resize, readback, and shutdown evidence.
- [x] No Render, Asset, Gameplay, or Editor code gains a native API dependency.

## Validation

This changes common lifetime behavior. Run Level 2 GraphicsContractTest and Level 3 GraphicsSmoke for both APIs. Build affected Render tests because RenderSystem consumes the recorder. Add targeted OpenGL recorder contract coverage where a display-independent path exists; runtime smoke remains mandatory.
