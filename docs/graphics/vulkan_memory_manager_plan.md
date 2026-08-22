# Vulkan Memory Manager Plan

**Status:** landed for Vulkan buffers (2026-08-22); image allocation remains on
the pre-existing image-specific allocator.
**Scope:** Vulkan buffer memory ownership, suballocation, host mapping, and
shutdown. This plan does not change the API-neutral RHI contract or OpenGL.

## Problem

`VulkanBufferManager` currently owns buffer handles, chooses allocation
strategies, calls `vkAllocateMemory`, and calls `vkMapMemory` for each buffer.
Small uniform buffers are suballocated from a shared `VkDeviceMemory` block.
`FrameContext` creates one such buffer for every frame slot and maps every one.

Vulkan maps `VkDeviceMemory`, not a `(buffer, offset, range)` allocation.
Consequently, mapping two different suballocations belonging to one pool block
causes the validation error:

```text
vkMapMemory(): memory has already been mapped
```

The current shape also has no authoritative mapped-state owner: buffer destroy
does not unmap memory, and `UploadData` may temporarily map memory already
persistently mapped by a frame arena.

## Target ownership

```text
VulkanBackend
  owns VulkanMemoryManager
    owns shared MemoryBlock objects (one VkDeviceMemory each)
    owns dedicated Allocation objects (one VkDeviceMemory each)
  owns VulkanBufferManager
    owns VkBuffer records
    holds a non-owning VulkanMemoryAllocation for each VkBuffer
```

`VulkanMemoryManager` is the sole type allowed to call `vkAllocateMemory`,
`vkMapMemory`, `vkUnmapMemory`, and `vkFreeMemory`. `VulkanBufferManager`
only creates/destroys `VkBuffer`, requests/releases allocations, and binds a
returned allocation with `vkBindBufferMemory`.

The manager is Vulkan-private. `RenderBackend`, `FrameContext`, and render code
continue to use `BufferHandle` plus an opaque mapped CPU pointer; they must not
learn about `VkDeviceMemory`, block IDs, or allocation policy.

## Core types

Use a single value-type allocation record for buffer binding and CPU writes:

```cpp
enum class VulkanAllocationPolicy
{
    SharedBlock,
    Dedicated,
};

struct VulkanMemoryAllocation
{
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    VkDeviceSize size = 0;
    std::byte* mapped_address = nullptr;

    uint32_t block_index = 0;
    class VulkanMemoryManager* manager = nullptr; // non-owning release route

    bool IsValid() const { return memory != VK_NULL_HANDLE; }
};
```

`VulkanMemoryAllocation` is metadata, not an independent owner of
`VkDeviceMemory`. It is copied into a `VulkanBufferResource`; destruction is
explicit at the buffer-manager ownership boundary so stale handle protection
remains compatible with the current handle system.

`VulkanMemoryManager` owns all native memory through move-only RAII objects:

```cpp
class VulkanMemoryBlock
{
public:
    VulkanMemoryBlock(const VulkanMemoryBlock&) = delete;
    VulkanMemoryBlock& operator=(const VulkanMemoryBlock&) = delete;
    VulkanMemoryBlock(VulkanMemoryBlock&&) noexcept = default;
    VulkanMemoryBlock& operator=(VulkanMemoryBlock&&) noexcept = default;
    ~VulkanMemoryBlock();

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkDeviceMemory memory_ = VK_NULL_HANDLE;
    std::byte* mapped_base_ = nullptr;
    VkDeviceSize size_ = 0;
    VkDeviceSize free_bytes_ = 0;
    uint32_t memory_type_index_ = 0;
};
```

The destructor performs, in order, `vkUnmapMemory` when mapped, then
`vkFreeMemory`. Destructors log failures only; they do not throw.

## Mapping model

Host-visible blocks use persistent mapping. A block is mapped once when it is
created, using the whole block range. Every host-visible suballocation receives
its CPU address without another Vulkan call:

```cpp
allocation.mapped_address = block.mapped_base() + allocation.offset;
```

This makes multiple frame-slot uniform arenas safe even when they occupy slots
in the same shared block. `MapUniformBuffer` becomes a compatibility accessor
that returns `allocation.mapped_address`; it must not call `vkMapMemory`.

Device-local blocks have a null `mapped_address`. Writes to device-local buffers
continue through an explicitly allocated host-visible staging buffer and copy
commands. `UploadData` must write through an existing mapped allocation rather
than map/unmap raw memory itself.

For non-coherent host-visible memory, add manager-owned `Flush` and `Invalidate`
operations. They align ranges to `nonCoherentAtomSize` before calling
`vkFlushMappedMemoryRanges` or `vkInvalidateMappedMemoryRanges`. Coherent
memory needs neither call, but the API should keep that decision private.

## Allocation policies

Both policies return the same `VulkanMemoryAllocation` record; they differ only
in their owner and release behavior.

| Policy | Use | Lifetime | Mapping |
| --- | --- | --- | --- |
| Shared block | small, frequently created host-visible buffers; frame uniform arenas; staging buffers | release returns the range to its block | map the block once; return base + offset |
| Dedicated | large buffers, allocations with dedicated-allocation requirements, or resources whose lifetime should not share a block | release unmaps and frees that allocation immediately | optionally persistent-map that one allocation if host-visible |

The initial dedicated threshold remains the current pool maximum (`4 MiB`) but
becomes a named configuration field. Allocation policy is also forced to
`Dedicated` when Vulkan reports a dedicated-allocation requirement. Do not add
memory-budget eviction or defragmentation in this milestone.

Blocks are segregated by at least memory type index, host-visible/coherent
properties, and allocation policy. Never suballocate incompatible Vulkan memory
types from one block.

## Public manager boundary

The initial interface should be deliberately small:

```cpp
class VulkanMemoryManager
{
public:
    explicit VulkanMemoryManager(VulkanDevice& device);
    ~VulkanMemoryManager();

    VulkanMemoryAllocation Allocate(
        const VkMemoryRequirements& requirements,
        VkMemoryPropertyFlags required_properties,
        VulkanAllocationPolicy policy);
    void Free(VulkanMemoryAllocation& allocation) noexcept;

    void Write(const VulkanMemoryAllocation& allocation,
               const void* source, VkDeviceSize size, VkDeviceSize offset = 0);
    void Flush(const VulkanMemoryAllocation& allocation,
               VkDeviceSize size, VkDeviceSize offset = 0);
    void Invalidate(const VulkanMemoryAllocation& allocation,
                    VkDeviceSize size, VkDeviceSize offset = 0);
};
```

`Allocate` validates required memory type bits, alignment, and size before it
returns. `Write` rejects device-local allocations and ranges outside the
allocation. There is no public raw `vkMapMemory` wrapper.

## Buffer-manager changes

1. Construct `VulkanMemoryManager` in `VulkanBackend`, before
   `VulkanBufferManager`, and inject it as a non-owning dependency.
2. In `CreateBufferResource`, query `VkMemoryRequirements`, choose required
   properties and allocation policy, request an allocation, and bind it.
3. Store the returned record in `VulkanBufferResource`.
4. Replace `MapBuffer` with an accessor that returns `mapped_address` and
   validates the requested range. Keep the RHI-facing method temporarily to
   avoid a broad OpenGL/render change.
5. Make `UploadData` call `VulkanMemoryManager::Write` for host-visible
   buffers. Keep staging/copy ownership in the backend where it already is.
6. In `DestroyBufferResource`, destroy the `VkBuffer`, then release its
   allocation exactly once through the memory manager.
7. Delete `VulkanBufferManager::FreeMemory`, both allocator maps, and direct
   `vkMapMemory`/`vkUnmapMemory`/`vkFreeMemory` calls from the buffer manager.

## Shutdown invariant

GPU work must no longer reference a buffer before its allocation is released.
The required teardown order is:

```text
RenderSystem waits idle
  -> releases FrameContext buffers and transient bindings
  -> releases cached static resources
  -> VulkanBackend destroys VkBuffer resources
  -> VulkanMemoryManager destroys blocks/dedicated allocations
  -> VulkanDevice is destroyed
```

`VulkanMemoryManager` must be destroyed before `VulkanDevice`. The manager must
also release dedicated allocations; the current `FreeMemory` path only visits
pooled allocators and would miss dedicated allocator state.

## Implementation slices

### Slice 1 — ownership seam and regression test

- Add `VulkanMemoryManager` with a shared-block implementation only.
- Make one host-visible block map exactly once and return `base + offset` for
  every allocation.
- Route uniform-buffer allocation through it.
- Add a Vulkan validation smoke path that creates at least two `FrameContext`
  instances from the same block and records several frame-slot rotations.
- Success: no `memory has already been mapped` validation error.

### Slice 2 — complete buffer migration

- Move all pooled buffer allocations from `VulkanBufferManager` to the manager.
- Replace temporary map/unmap uploads with manager writes.
- Add explicit non-coherent flush support even if the preferred desktop memory
  type is coherent.
- Success: buffer manager contains no `VkDeviceMemory` lifetime calls.

### Slice 3 — dedicated allocations

- Add dedicated RAII allocation objects and policy selection by size.
- Honor dedicated-allocation requirements where exposed by Vulkan.
- Verify dedicated allocations unmap/free on individual buffer destruction;
  shared allocations only return their slot.
- Success: large and small host-visible buffers coexist without shared mapping
  violations or leaks.

### Slice 4 — hardening

- Add range, alignment, double-free, and stale-allocation assertions.
- Add validation smoke coverage for allocate/write/free, frame-context reuse,
  and backend shutdown.
- Add lightweight manager statistics: allocated bytes, live bytes, block count,
  dedicated allocation count, and failed allocation reason.
- Update `graphics_module.md`, `vulkanbackend.md`, `TODO.md`, and `status.md`
  only after the implementation lands.

## Non-goals

- No cross-API memory allocator abstraction.
- No Vulkan Memory Allocator dependency in this milestone.
- No GPU defragmentation, aliasing, sparse binding, memory budget eviction, or
  render-graph transient resource allocator.
- No synchronization change: `VulkanFrameContext` remains the authority for
  fence-based frame-slot reuse.

## Acceptance criteria

- One shared host-visible `VkDeviceMemory` block is mapped at most once during
  its lifetime.
- Every host-visible allocation exposes a stable `mapped_address` for as long
  as the allocation is live.
- A buffer never outlives its allocation, and a memory block never outlives the
  Vulkan device.
- Releasing a shared allocation cannot unmap its block while another allocation
  is live.
- Releasing a dedicated allocation unmaps and frees only that allocation.
- Validation layers report no mapped-memory, invalid-range, or free-while-mapped
  errors in the Vulkan smoke scene.
