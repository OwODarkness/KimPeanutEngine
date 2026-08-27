# Graphics Bindless Textures

- Status: partial
- Date: 2026-08-27
- Spec: [Graphics Bindless Textures](../specs/graphics-bindless-textures.md)
- Parent TODO: [Bindless textures — planned, optional path](../../docs/graphics/TODO.md#bindless-textures--planned-optional-path)

## What was done

- Recorded the bindless-texture work as a risky, multi-session graphics task.
- Captured the existing B0–B5 roadmap as an implementation spec, including the
  architecture boundary, required fallback, frame-safe reuse, and validation
  expectations.
- Confirmed that no bindless implementation has started: current Vulkan and
  OpenGL capability reports intentionally keep `bindless_textures` disabled.

## What changed

- Architecture or behavior: none. This checkpoint creates planning records
  only; it does not change graphics, render, shader, or backend behavior.
- Important files/modules: `.spec/specs/graphics-bindless-textures.md`, this
  journal, `docs/graphics/TODO.md`, and `docs/graphics/graphics_module.md`.
- Public API or ownership changes: none. The proposed ownership remains
  Graphics for native table/lifetime state and Render for material policy.

## Validation

- Required level: L0 — documentation-only checkpoint.
- Command: inspected `.spec/README.md`, `.spec/journal/README.md`,
  `docs/graphics/TODO.md`, `docs/graphics/graphics_module.md`, and
  `docs/validation_matrix.md`.
- Result: PASS — the recorded plan matches the existing B0–B5 roadmap and
  status; no source code changed.
- Evidence: the planned path documents the common contract at
  `docs/graphics/graphics_module.md` and the staged work in
  `docs/graphics/TODO.md`.

## Remaining risks and unverified areas

- All implementation risks remain open: generational slot protocol, descriptor
  update visibility, deferred reuse after submitted work, table ABI, Vulkan
  feature enablement, OpenGL resident-handle behavior, and fallback output
  equivalence.
- No build, contract test, or runtime smoke was run because this checkpoint
  adds documentation only.

## Remaining work

- Start at B0 by making the common contract and shader-table ABI decisions
  explicit, then update this journal only at a meaningful implementation,
  validation, pause, or blocker checkpoint.

## Documentation and follow-up

- Keep `docs/graphics/TODO.md` as the roadmap and this spec as the durable
  decision record. Append dated corrections/checkpoints here rather than
  rewriting historical results.

## Checkpoint — 2026-08-27

- Done: completed B0. Added the public sampled-texture-table ABI (`v1`, set 1,
  binding 0, maximum 4096 slots), `BindlessTextureHandle`, capability capacity,
  and default-unavailable backend allocation/release API. Marked the B0 roadmap
  items complete; no backend or material path is enabled.
- Validation: `./tools/kp.ps1 build GraphicsContractTest` — NOT RUN to
  compilation. MSBuild failed while evaluating Windows SDK properties because
  access to `C:\Users\17519\AppData\Local\Microsoft SDKs` was denied; this is
  an environment failure before project source compilation.
- Static review: `git diff --check` — PASS. Existing positional initialization
  search found no production `GraphicsCapabilities` initializer that needs an
  update for the new capacity field.
- Risk: B1 must still define update visibility and deferred reuse before any
  backend is permitted to return a valid slot.

## Checkpoint — 2026-08-27 (B1)

- Done: added the CPU-only `BindlessTextureSlotAllocator` and telemetry. A
  release invalidates its handle immediately and quarantines the slot until
  `CollectCompleted` receives the submitted-work serial supplied at release.
  The protocol fixes update visibility at a frame boundary and bans normal
  `WaitIdle` use. B1 does not create a native descriptor table or resident
  handle; that remains B2/B3.
- Validation: `./tools/kp.ps1 build GraphicsContractTest` — NOT RUN to
  compilation. The same MSBuild Windows SDK property evaluation failed because
  access to `C:\Users\17519\AppData\Local\Microsoft SDKs` was denied, before
  project sources compiled.
- Risk: a backend must accurately include the current recording frame's future
  submission serial when releasing a slot; B2 will bind this policy to Vulkan
  fences and descriptor updates.

## Correction — 2026-08-27

- Moved `BindlessTextureSlotAllocator` implementation from the public contract
  header into `backend/common/bindless_texture.cpp`. `Graphics` and
  `GraphicsContractTest` now compile that implementation explicitly. The
  lifetime contract and behavior are unchanged.
- Validation: `cmake -S . -B build` — PASS. The subsequent
  `./tools/kp.ps1 build GraphicsContractTest` remains blocked before compilation
  by the existing Windows SDK directory access denial.

## Checkpoint — 2026-08-27 (B2 scope)

- Decision: B2 is an atomic Vulkan milestone. Descriptor-indexing enablement,
  private table allocation, compatible pipeline layouts, command-recorder
  binding, B1 retirement from exact completed submission serials, and common
  capability enablement must land and validate together.
- Guardrail: a descriptor table that cannot be bound and sampled by a
  compatible pipeline is not marked as B2 progress and must not advertise
  bindless support.

## Checkpoint — 2026-08-27 (B2 complete)

- Done: completed the atomic Vulkan milestone. `VulkanDevice` probes and enables
  the required descriptor-indexing subset only when the selected physical device
  supports it. `VulkanBindlessTextureTable` privately owns one update-after-bind,
  partially-bound sampled-image descriptor set for each frame slot. It applies
  entries only after the corresponding slot fence completes; it uses the common
  allocator and completed submission serials to defer physical-index reuse and
  retain old texture/sampler references until safe.
- Pipeline/recording: Vulkan pipeline layouts reserve the V1 set-1 table while
  the capability is enabled, and `VulkanCommandRecorder` binds that frame's
  table when it binds a pipeline. Resource shader processing now exposes the
  versioned ABI set/binding macros without a Resource -> Graphics dependency.
  Ordinary set-0 material bindings and the OpenGL path remain unchanged.
- Capability/fallback: a descriptor-indexing feature advertisement alone is not
  enough. `GraphicsCapabilities` reports bindless only after device enablement
  and table creation succeed; unsupported devices report zero capacity and the
  default backend acquire call remains invalid.
- Runtime evidence: the smoke scene acquires a resolved material texture when
  Vulkan reports support, observes frame-boundary table application and recorder
  binding, then releases the slot before idle/teardown. It continues through
  the existing ordinary material binding, which keeps this B2 validation
  independent of B4 material-policy adoption.
- Validation: `./tools/kp.ps1 build GraphicsContractTest` — PASS;
  `GraphicsContractTest.exe` — PASS (10 tests); `./tools/kp.ps1 build
  GraphicsSmoke` — PASS; `GraphicsSmoke.exe` — PASS on NVIDIA GeForce RTX 4070
  Laptop GPU through Vulkan and OpenGL. `ctest --test-dir build -C Debug -R
  GraphicsContractTest` found no registered tests, and `./tools/kp.ps1 smoke`
  is currently blocked by its empty-argument wrapper defect; neither is a
  source failure.
- Remaining risk: B4 still needs a material template that selects a compatible
  shader's table lookup and passes the common slot index, then B5 needs a
  multi-material output-equivalence test. B2 proves the backend table can be
  populated and bound, but the current fallback shader intentionally does not
  sample it yet.

## Checkpoint — 2026-08-27 (B3 complete)

- Done: completed the OpenGL backend path. It requires
  `GL_ARB_bindless_texture` and `GL_ARB_gpu_shader_int64`, dynamically resolves
  the resident-handle entry points after GL loader initialization, and otherwise
  leaves the capability unavailable. `OpenglBindlessTextureTable` owns the
  GPU-visible SSBO of 64-bit resident handles and binds it at the V1 logical
  table binding whenever a pipeline is bound.
- Lifetime: OpenGL's existing one-frame backend now fences each submitted table
  frame. The next `BeginFrame` waits that fence before applying queued table
  writes, while released handles stay resident and their slots quarantined until
  the matching completion serial. Texture/sampler destruction rejects both live
  and retired references; `WaitIdle` is used only for shutdown cleanup.
- Validation: `./tools/kp.ps1 build GraphicsSmoke` — PASS;
  `GraphicsSmoke.exe` — PASS through Vulkan and OpenGL on the local RTX 4070
  Laptop GPU. The common smoke allocates a material texture slot for every
  backend that advertises support, so the OpenGL run exercises acquisition,
  next-frame table visibility, SSBO binding, release, and safe teardown.
- Remaining risk: B4 remains responsible for serializable material opt-in,
  passing the common slot index, and a shader that samples it. B5 must add a
  multi-material output-equivalence check.

## Checkpoint — 2026-08-27 (B4 complete)

- Done: `MaterialTemplateDesc` now has serializable, API-neutral opt-in
  metadata. `RenderResourceResolver` acquires one common table slot per ready
  material texture only when both the template and backend support V1; it
  releases the slots on re-resolution, instance release, and cleanup. A failed
  acquisition retires all partial slots and leaves the existing bound path.
- Frame data: `FrameContext` selects that resolved mode per instance. Its
  binding-3 material UBO starts with one std140 `uvec4` per parameter, indexed
  by parameter ID; `.x` stores the common generational texture-table slot.
  Bindless-compatible shaders consume that prefix, while ordinary shaders see
  their unchanged sampled-texture bindings because the metadata remains false.
- Validation: `./tools/kp.ps1 build RenderPassScheduleTest` and
  `./tools/kp.ps1 build GraphicsSmoke` passed. `RenderPassScheduleTest.exe`
  passed 25 tests, including the metadata persistence case; `GraphicsSmoke.exe`
  passed on Vulkan and OpenGL with bindless capability enabled on the local RTX
  4070 Laptop GPU. B5 still owns the first real compatible shader and
  multi-material visual-equivalence scene.

## Checkpoint — 2026-08-27 (B5 implementation evidence)

- Shader variants: material templates can now carry an optional bindless shader
  program. The resolver selects it only for an enabled table; otherwise it
  keeps the ordinary program and its set-0 sampled-texture binding. The
  pipeline cache includes this binding-model distinction.
- Runtime evidence: `simple_triangle.frag`, compiled with `KP_USE_BINDLESS=1`,
  uses the V1 material index block
  and target API macro to sample Vulkan's descriptor array or OpenGL's resident
  handle SSBO. `GraphicsSmoke` draws wallpaper and default textures through a
  bindless pair and an ordinary bound pair, checking slot coverage and the
  selected mode for every draw.
- Validation: `GraphicsContractTest.exe` passed 10 tests;
  `RenderPassScheduleTest.exe` passed 25 tests; `GraphicsSmoke.exe` passed on
  Vulkan and OpenGL with bindless enabled on the local RTX 4070 Laptop GPU.
  The smoke has no render-target readback, so its output-equivalence evidence
  is execution of side-by-side equivalent shaders rather than pixel comparison.
  The remaining B5 checklist items are automated forced-unavailable/capacity
  selection tests and an image comparison/readback path.
