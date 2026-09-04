# LO1 — Asset Load Observation

- Status: complete
- Parent roadmap: [Asset Loading Progress TODO](../TODO.md#lo1--asset-load-observation)
- Cross-stage spec: [Asset Loading Progress](../../../.spec/specs/asset-loading-progress.md)

## Objective

Add an opt-in, read-only observation session around existing Asset loads so
Runtime can obtain truthful phase, count, timing, size, and failure facts. LO1
must not change Asset identity, payload/dependency ownership, loader scheduling,
or the result of an existing `LoadSync`/`LoadAsync` call.

## Current path and design boundary

`AssetManager::LoadSync` currently performs:

```text
classify path
-> initial cache lookup
-> wait for load_mutex_
-> LoadByExtension
-> recursively LoadSync dependency requests
-> final cache lookup and RegisterAsset under state_mutex_
-> return AssetID
```

`LoadAsync` runs that same path through `std::async`; loaders still serialize.
Model and shader-program loaders also register generated Mesh/Shader subassets
inside `LoadByExtension`. An `Asset` wrapper exists only after registration, so
load execution state cannot live on `Asset` or use `AssetID` as its identity.

LO1 observes only this Asset-owned path. Resource processing, shader
compilation, environment derivation, GPU upload, and level instantiation remain
outside its success definition and will be aggregated by Runtime in LO2.

## Final decisions

| Question | LO1 decision |
|---|---|
| Activation | Explicit `AssetLoadSession`; existing overloads remain unobserved. |
| State owner | Opaque session state created by Asset and shared by session handles/active operations. |
| Runtime storage | Runtime may retain the session handle and compact snapshot only; it does not own operation records. |
| Operation identity | Monotonic session-local `AssetLoadOperationID`, distinct from `AssetID`. |
| Recursive correlation | A private `LoadSyncObserved` helper passes session state and parent operation ID. |
| Snapshot size | Exact aggregate plus at most 16 active and 16 recent terminal details. |
| Terminal retention | Fixed 16-record ring; exact aggregate and first failure survive until session destruction. |
| Session completion | Caller seals after scheduling roots; terminal means sealed and active count is zero. |
| Cancellation | Not represented in LO1; Runtime cancellation remains LO2 work. |
| Timing clock | `std::chrono::steady_clock`, published as integer microseconds. |
| Size V1 | Cold source-file bytes and logical decoded payload bytes when cheaply measurable. |
| Deferred metrics | Actual bytes read, allocator overhead, transient peak memory, and per-byte progress. |

The fixed detail limits are contract constants, not configuration. They keep
the startup-facing snapshot small. A future Asset profiler may add a separate
paged diagnostic query; it must not enlarge this snapshot.

## Public contract

Place public value/handle types in `engine/runtime/asset/asset_load_observation.h`.
Names may receive minor style adjustments during implementation, but semantics
and ownership must remain equivalent to:

```cpp
namespace kpengine::asset
{
    namespace detail
    {
        class AssetLoadSessionState;
    }

    using AssetLoadSessionID = uint64_t;
    using AssetLoadOperationID = uint64_t;

    enum class AssetLoadState : uint8_t
    {
        Running,
        Succeeded,
        Failed,
    };

    enum class AssetLoadPhase : uint8_t
    {
        CacheLookup,
        WaitingForLoader,
        LoadSource,
        ResolveDependencies,
        Register,
    };

    enum class AssetLoadDisposition : uint8_t
    {
        CacheHit,
        LoadedAndRegistered,
        LoadedThenDeduplicated,
    };

    struct AssetLoadTiming
    {
        uint64_t cache_lookup_us = 0;
        uint64_t loader_queue_wait_us = 0;
        uint64_t source_load_us = 0;
        uint64_t dependency_wait_us = 0;
        uint64_t registration_us = 0;
        uint64_t inclusive_elapsed_us = 0;
    };

    struct AssetLoadSizeCost
    {
        std::optional<uint64_t> source_file_bytes;
        std::optional<uint64_t> decoded_payload_bytes;
    };

    struct AssetLoadObservation
    {
        AssetLoadOperationID operation = 0;
        std::optional<AssetLoadOperationID> parent;
        std::string display_path;
        AssetType expected_type = AssetType::Undefined;
        AssetLoadState state = AssetLoadState::Running;
        AssetLoadPhase phase = AssetLoadPhase::CacheLookup;
        uint32_t completed_children = 0;
        uint32_t known_children = 0;
        std::optional<AssetLoadDisposition> disposition;
        std::optional<AssetID> result;
        AssetLoadTiming timing;
        AssetLoadSizeCost size_cost;
        std::string diagnostic;
    };

    struct AssetLoadCostTotals
    {
        uint64_t cumulative_cache_lookup_us = 0;
        uint64_t cumulative_loader_queue_wait_us = 0;
        uint64_t cumulative_source_load_us = 0;
        uint64_t cumulative_registration_us = 0;
        uint64_t measured_source_file_bytes = 0;
        uint64_t measured_decoded_payload_bytes = 0;
        uint32_t source_file_measurement_count = 0;
        uint32_t decoded_payload_measurement_count = 0;
    };

    struct AssetLoadSummary
    {
        uint32_t operations_started = 0;
        uint32_t operations_active = 0;
        uint32_t operations_succeeded = 0;
        uint32_t operations_failed = 0;
        uint32_t cache_hits = 0;
        uint32_t post_load_deduplications = 0;
        uint64_t wall_elapsed_us = 0;
        AssetLoadCostTotals cost;
        std::string first_failure;
    };

    struct AssetLoadSnapshot
    {
        AssetLoadSessionID session = 0;
        uint64_t revision = 0;
        bool sealed = false;
        bool terminal = false;
        uint32_t omitted_active_operations = 0;
        AssetLoadSummary summary;
        std::vector<AssetLoadObservation> active_operations;
        std::vector<AssetLoadObservation> recent_terminal_operations;
    };

    class AssetLoadSession
    {
    public:
        AssetLoadSession() = default;
        bool IsValid() const noexcept;
        void Seal() noexcept;
        AssetLoadSnapshot GetSnapshot() const;

    private:
        explicit AssetLoadSession(
            std::shared_ptr<detail::AssetLoadSessionState> state);
        std::shared_ptr<detail::AssetLoadSessionState> state_;
        friend class AssetManager;
    };
}
```

`AssetLoadSession` is a cheap copyable handle. Its hidden
`shared_ptr<detail::AssetLoadSessionState>` lets `LoadAsync` and recursive
operations safely outlive the initiating stack. The state is destroyed
automatically after the caller and all active operations release their copies;
no global session registry or explicit acknowledgement API is needed.

Session ID zero and operation ID zero are invalid. `BeginLoadObservation` uses
a process-wide atomic counter for nonzero session IDs; each session assigns
operation IDs from one under its own lock. An invalid session returns a default
invalid snapshot and `Seal()` is a no-op.

Extend `AssetManager` without changing existing call sites:

```cpp
AssetLoadSession BeginLoadObservation();

AssetID LoadSync(const std::string &path);
AssetID LoadSync(const std::string &path, const AssetLoadSession &session);

std::future<AssetID> LoadAsync(const std::string &path);
std::future<AssetID> LoadAsync(const std::string &path,
                               AssetLoadSession session);
```

All overloads delegate to one private `LoadSyncInternal` pipeline carrying a
nullable session state, optional parent operation ID, and optional pre-reserved
root operation. The original overload passes null and takes no observation lock
or allocation path. Recursive dependency calls use the same helper, preventing
observed/unobserved load semantics from drifting. Observed `LoadAsync`
pre-reserves its root operation before dispatch and passes that ID into the
helper. Passing an invalid or already sealed session must never change whether
the Asset load succeeds; assert/log in debug and continue unobserved.

`display_path` is the Asset-root-relative lexical path when the request is
under `GetAssetDirectory()` and otherwise only the source filename. Normalize
separators without resolving symlinks or touching the filesystem. Do not expose
a separate raw absolute-path field in the startup snapshot; existing logs
remain the detailed developer diagnostic surface.

## Mutable state and bounded snapshot

The hidden session state owns:

- one mutex used only for observation state;
- session ID, revision, start time, sealed flag, and next operation ID;
- exact aggregate counters and cost totals;
- all currently active operation records;
- a 16-entry FIFO/ring of recent terminal records;
- the first failure diagnostic independent of the terminal ring.

Active records cannot be discarded while their work is running, but snapshot
publication sorts them by descending operation ID and copies the 16 most recent,
so the currently entered recursive leaf is retained. The snapshot reports the
omitted count. Terminal completion updates the exact aggregate, removes the
active record, and appends one bounded terminal detail. Successful details may
roll out of the ring; their aggregate counts/costs remain exact.

Runtime polls `GetSnapshot()` and copies only this bounded value. It does not
replay revisions or retain every operation. LO2 may project the Asset summary
into its smaller cross-subsystem snapshot and drop the Asset snapshot entirely
after the session becomes terminal.

Every externally visible mutation increments `revision`. Snapshot construction
holds only the observation mutex, computes active/session elapsed time using the
injected/default steady clock, copies bounded values, then releases the lock.

## State and session rules

- An operation begins as `Running/CacheLookup` and terminates exactly once as
  `Succeeded` or `Failed`.
- An initial cache hit succeeds with disposition `CacheHit`, a valid result,
  and no loader/size-probe work.
- A cold registration succeeds as `LoadedAndRegistered`. If another request
  wins the final dedup race after this operation performed loader work, report
  `LoadedThenDeduplicated`; do not mislabel that wasted work as a cache hit.
- Every success contains a valid result and its applicable terminal cost
  measurements.
- Failure contains a stable diagnostic. The first failure is retained in the
  summary even after its detail rolls out of the terminal ring.
- A parent publishes `known_children` immediately after loader-declared
  dependency requests are known and increments `completed_children` after each
  recursive child returns, whether that child succeeded or failed.
- The parent succeeds only after all required children succeed and its own
  Asset is registered.
- Two concurrent requests remain distinct operations even if the current final
  dedup check gives both the same `AssetID`. LO1 does not coalesce loader work.
- `Seal()` is idempotent. A session becomes terminal only when sealed and no
  active operations remain. Its wall time stops at that transition. An empty
  sealed session is valid and terminal.
- Observed `LoadAsync` reserves its root operation synchronously before
  returning the future. The caller may therefore seal immediately after
  scheduling; the worker continues the already-admitted operation rather than
  being rejected as a post-seal load.
- LO1 has no `Cancelled` Asset state because current decoders cannot be stopped
  cooperatively. Runtime may stop scheduling new roots in LO2.

## Timing attribution

Measure at the existing boundaries:

| Field | Start | Stop | Notes |
|---|---|---|---|
| `cache_lookup_us` | before initial cache lookup | after its Asset lock is released | Initial lookup only. |
| `loader_queue_wait_us` | immediately before `load_mutex_.lock()` | immediately after acquisition | Contention only; excludes loader work. |
| `source_load_us` | immediately before `LoadByExtension` | immediately after it returns/throws | Includes loader-generated subasset registration. |
| `dependency_wait_us` | before recursive dependency loop | after loop terminates | Inclusive of child work; diagnostic only. |
| `registration_us` | before final dedup/register critical section | after Asset lock is released | Includes the final dedup check and optional parent registration, so it does not overlap `cache_lookup_us`. |
| `inclusive_elapsed_us` | operation creation | terminal publication/current snapshot | Includes waits and recursive children. |
| summary `wall_elapsed_us` | session creation | terminal/current snapshot | Never derived by summing operation totals. |

Use `std::chrono::steady_clock`. Convert with
`duration_cast<microseconds>` and clamp negative injected-test deltas to zero.
Terminal values never change after publication.

Aggregate only cache lookup, queue wait, source load, and registration fields
from all terminal attempts, including failures and post-load deduplications. Do
not aggregate `inclusive_elapsed_us` or
`dependency_wait_us`: parent values include child time and would double-count
the dependency tree. Cumulative queue wait may overlap between threads and is
explicitly a summed operation cost, not wall time.

## Size attribution

For an attempt that proceeds past the initial cache check, query
`std::filesystem::file_size(path, error_code)` at
most once, outside Asset and observation locks. Failure to obtain the size is
non-fatal and leaves `source_file_bytes` empty. A cache hit performs no file
metadata query and leaves both size fields empty.

`decoded_payload_bytes` means logical retained CPU buffer bytes, not allocator
residency. Compute it without scanning buffer contents:

| Loaded type | LO1 measurement |
|---|---|
| Texture | `TextureData::pixels.size()` |
| Audio | `AudioClip::pcm.size() * sizeof(float)` |
| Model | Sum vertex, index, and section vector logical bytes for Mesh dependencies generated by that model load. |
| Mesh | Same vector calculation when a Mesh payload is directly observable. |
| Shader/ShaderProgram | Unavailable at Asset stage; compiled/source artifacts belong to Resource. |
| Material/Level | Unavailable in V1; structural strings/vector allocator cost is not measured. |

Use checked `uint64_t` addition/multiplication. Overflow or unavailable payload
data yields an empty measurement, not a saturated or partial value. Do not
include dependency payload bytes in a Level/Material operation: those children
own their measurements. Model is the exception because its Assimp-generated
Mesh is registered inside the source-loader operation and has no separate load
operation.

Actual bytes read, vector capacity/allocator overhead, Assimp/stb/miniaudio
transient buffers, and peak memory remain explicitly deferred. Reporting
`file_size` as bytes read would be false.

## Locking and exception safety

The non-negotiable rule is:

```text
Asset locks -> released -> observation update
observation lock -> never acquire load_mutex_ or state_mutex_
```

Do not publish an observation while `load_mutex_` or `state_mutex_` is held.
Collect IDs, results, durations, and diagnostics locally; update the session
after releasing the Asset lock. Snapshot code touches only session state.

Use a small private operation guard so every early return reaches one terminal
publication. Surround the observed wrapper with `try`/`catch`: publish the
exception diagnostic, then rethrow so existing exception behavior is
preserved. The guard destructor is `noexcept` and provides only a fixed fallback
failure if an unexpected path abandons a running operation.

Observation allocation, size probing, or bookkeeping failure must not convert a
successful Asset load into a failure. Disable further detail for that session,
retain a compact observation diagnostic if possible, and preserve the original
load result/exception.

## File and API change map

Expected implementation changes:

- Add `engine/runtime/asset/asset_load_observation.h/.cpp` for public values,
  session handle behavior, and bounded snapshot construction.
- Add `engine/runtime/asset/asset_load_observation_internal.h` for the private
  session-state recorder, operation guard, and injectable clock seam. It is an
  Asset implementation/testing header, not a Runtime or Editor contract.
- Add the new source explicitly to `engine/runtime/asset/CMakeLists.txt`.
- Extend `engine/runtime/asset/asset_manager.h/.cpp` with observed overloads and
  a private recursive helper; preserve original overload signatures.
- Keep payload-size helpers private to the Asset implementation unless another
  current consumer appears.
- Add `engine/test/unit/asset/asset_load_observation_test.cpp` and register it
  in the existing `AssetUnitTest` target.
- Extend existing level/model tests only for integration behavior that cannot
  be proven through the observation-state unit tests.

Do not change loader interfaces, introduce an event bus, add a global observer,
refactor `AssetManager` singleton ownership, parallelize loaders, or modify
unload/dependency eviction in LO1.

## Implementation sequence

1. Add the standalone internal session-state recorder with an injectable steady
   `Now` function for deterministic unit tests and wrap it with the public
   `AssetLoadSession` handle.
2. Prove seal/terminal behavior, revision increments, bounded terminal detail,
   active truncation/omitted count, first-failure retention, and snapshot copy
   isolation without `AssetManager`.
3. Add observed `LoadSync` and private recursive parent propagation while
   leaving the old overload unchanged.
4. Instrument phase/timing boundaries and exception-safe terminal publication.
5. Add best-effort cold source-file size and the V1 decoded-size helpers.
6. Add observed `LoadAsync`, capturing a session handle by value and preserving
   the current future-destruction behavior. Reserve its root operation before
   dispatch so immediate caller sealing is race-free.
7. Add AssetManager integration/concurrency tests and run focused validation.
8. Review the diff for lock nesting, accidental loader/API changes, unbounded
   storage, and changes to unobserved behavior before marking LO1 complete.

## Acceptance criteria

- [x] Existing unobserved calls return the same IDs/errors and take no
  observation mutex or allocate observation records.
- [x] A valid session observes root and recursive dependency operations with
  stable parent IDs, exact counters, and one terminal state per operation.
- [x] Snapshots contain no payload pointers, futures, mutable views, raw
  absolute-path field, or more than 16 active/16 terminal detail records.
- [x] Aggregate counts and supported byte totals remain exact after terminal
  details roll out of the ring.
- [x] Active elapsed values advance; terminal values are stable; session wall
  time is independent of summed operation-inclusive time.
- [x] Cache hits perform no loader work or measurement-only file query.
- [x] Initial cache hits and post-load deduplications have distinct dispositions
  and counters; the latter retains its loader timing/size cost.
- [x] Unsupported extension, loader failure, dependency failure, registration
  failure, and thrown exception all publish failure without changing existing
  return/throw behavior.
- [x] Concurrent observed requests remain coherent, preserve final Asset dedup,
  and do not deadlock.
- [x] Destroying caller session handles during `LoadAsync` is safe because the
  active operation owns session state until completion.
- [x] Sealing immediately after scheduling observed async roots cannot make
  them disappear or cause the session to become terminal prematurely.
- [x] Sealing is idempotent and terminal becomes true exactly when sealed and
  active count reaches zero.
- [x] No observation path acquires an Asset lock while holding the observation
  mutex or calls external code under either lock family.

## Validation commands

```powershell
.\tools\kp.ps1 build AssetUnitTest
.\tools\kp.ps1 test AssetLoadObservation
```

Also run `git diff --check`. If a shared Asset header or CMake dependency change
affects additional targets, follow the repository validation matrix and build
the impacted consumers. LO1 alone does not require runtime visual validation;
that evidence belongs to LO2/LO3.

## Deferred follow-ups

- Loader-reported actual bytes read and byte/item progress.
- Instrumented versus estimated transient peak memory.
- Paged per-asset profiling history beyond the bounded startup snapshot.
- Shared underlying-work identity if concurrent load coalescing is implemented.
- Cooperative Asset cancellation.
