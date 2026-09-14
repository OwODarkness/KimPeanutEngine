# AP1.5 — Loose-product scheduling and residency

- Status: AP1.5c range-aware initial texture reads landed; bounded streaming
  follow-up remains
- Parent: [AP1 — Startup Asset Loading Performance](AP1.md)
- Roadmap: [Asset Module TODO](../TODO.md#startup-performance-roadmap)

## Boundary

The engine is still an editor/development runtime. AP1.4 package mounting is
deferred until a publish/shipping consumer exists, so this slice optimizes the
existing loose `.archive` products only. It preserves AssetID identity,
dependency ownership, observation output, and the current complete-mip render
contract.

## AP1.5a acceptance

- Loader concurrency is explicit in the Asset type descriptor.
- Built-in stateless product readers may run concurrently, while foreign model,
  audio, and custom loaders remain serialized by default.
- A fixed semaphore bounds concurrent loader callbacks and the decode fan-out.
- Independent dependency requests are launched concurrently and committed in
  declaration order after all results are collected.
- Concurrent ordinary root requests share one in-flight result and do not
  decode/register the same path twice.
- Observed session roots retain distinct observations; complete mip readiness
  remains unchanged.

## AP1.5b initial residency slice

Native texture products remain complete, hash-verified loose files. The Asset
loader can now publish a tail-mip view for startup while retaining a bounded
background full-resolution read (two concurrent full-texture decodes). The
background loader is lazy: Render starts it only after the initial scene view
has committed, so its disk I/O cannot extend the Asset startup phase. Render
tracks those futures, re-resolves material bindings when a full view is ready,
and retires the previous GPU texture after the backend's frames-in-flight
grace period. RGBA16F environment textures intentionally stay full-resolution
for the current IBL preparation contract.

The initial mip policy is configured by Runtime and defaults to six levels for
the editor startup path; direct loaders, tools, and tests retain full loading
until they opt in. The native container schema and AssetID identity do not
change.

## AP1.5c range-aware initial reads

The editor/development loader trusts the bounded native texture header and mip
directory long enough to publish the requested resident tail. It reads only
the header/directory and one contiguous payload range beginning at the first
resident mip; it does not hash or allocate the complete product on the Asset
startup path. The existing lazy full-resolution task remains the background
verification path: it reads the complete immutable product, verifies its
digest and content-addressed archive location, and only then promotes the full
CPU view for Render.

This is intentionally an optimistic development-time policy. A malformed
header/directory or resident range still fails synchronously, while corruption
outside the resident range is reported by the background verification. A
future package/TOC or per-chunk-hash path may make that verification trusted
without rereading the complete loose product, but it is not required for the
current editor workflow.

Remaining AP1.5 work is true GPU subresource upload for in-place promotion and
a measured scene-level residency budget.
