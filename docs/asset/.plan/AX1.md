# AX1 — Extensible Asset Types and Polymorphic Payloads

- Status: active; AX1.4 landed 2026-09-08
- Parent roadmap: [Asset Module TODO](../TODO.md)
- Module architecture: [Asset Module Plans](../PLANS.md)
- Live2D consumer: [L2D2 — Live2D Asset integration](../../live2d/.plan/L2D2.md)

## Objective

Make the Asset module extensible for future asset kinds without adding a new
`std::variant` alternative, a new hard-coded suffix branch, or a new
`AssetManager` loader member for every feature module.

The Asset module owns the generic extension mechanism. Optional modules such as
Live2D only provide their format-specific payload and loader/importer
implementation through that mechanism.

AX1.1 landed the polymorphic payload boundary. AX1.2 provides the Asset-owned
runtime type/loader registry and reserves a stable custom range. AX1.3 provides
the separate offline importer/provider registry. AX1.4 hardens the extension
transaction boundary and documents the consumer-facing registration contract.

## Design question

How can a new asset type provide a CPU-side resource while preserving Asset's
identity, caching, dependency, rollback, observation, and unload semantics?

The target is a single polymorphic payload contract plus Asset-owned type and
loader registration:

```cpp
class IAssetPayload
{
public:
    virtual ~IAssetPayload() = default;
    virtual AssetType GetAssetType() const noexcept = 0;
};

using AssetPayload = std::shared_ptr<IAssetPayload>;

struct MeshResource final : IAssetPayload { /* ... */ };
struct TextureResource final : IAssetPayload { /* ... */ };
struct Live2DModelResource final : IAssetPayload { /* ... */ };

template <typename T>
std::shared_ptr<T> Asset::GetResource() const
{
    return std::dynamic_pointer_cast<T>(resource);
}
```

The base interface is the ownership and type-erasure boundary. Concrete
payloads remain ordinary C++ types owned by their defining module. Asset does
not need to include Live2D, and adding a future payload does not modify a
central variant or visitor list.

## Ownership and lifetime

- `AssetManager` owns the `Asset` wrapper and its identity/cache record.
- `Asset` owns one `AssetPayload` shared pointer to an immutable CPU payload.
- A caller receiving `GetResource<T>()` holds a shared reference and may outlive
  the `Asset` wrapper.
- `IAssetPayload` has no GPU handle, backend pointer, render-thread state, or
  mutable per-instance character state.
- A payload's `GetAssetType()` must agree with the `AssetID` type and the
  registered loader descriptor before cache publication.
- A Live2D model payload is shared immutable data; Cubism model parameters and
  deformed vertex data belong to independently owned runtime instances.

`std::variant` remains valid for closed, format-local value choices such as a
material parameter or level record. It is not used as the top-level Asset
payload because every new asset kind would require editing Asset's central
storage and all visitors.

## Asset type identity

The current closed enum must become an Asset-owned extensible value contract.
The migration must preserve every existing built-in numeric value and the
current `[type:16][generation:16][slot:32]` `AssetID` packing. Built-in names
remain stable constants; optional modules receive an explicitly registered
stable type value from the extension range.

The type registry owns:

- stable type value and canonical name;
- native product suffixes and collision rules;
- the expected payload type or payload validation policy;
- the loader factory/callback and its lifetime;
- optional display/measurement metadata used by Asset diagnostics.

Registration is explicit at the application or tool composition root, before
the first load. After sealing, descriptors are immutable and there is no V1
unregister operation. This matches the current statically linked module model
and avoids callbacks or payload types outliving a removed module.

The registry is generic. It must not contain `Live2DModelResource`, Cubism
headers, `.model3.json`, or any other feature-specific name.

## Loader and importer boundaries

Runtime loading and offline importing remain separate contracts:

```text
runtime Asset type registry
  -> native suffix
  -> registered loader
  -> AssetRegisterInfo
  -> AssetManager cache/dependency/rollback transaction

offline Asset importer registry
  -> source suffix or explicit importer name
  -> importer provider
  -> deterministic native product publication
```

Runtime loaders decode native products and return generic `AssetRegisterInfo`.
They do not allocate `AssetID`s, mutate caches, recursively call
`AssetManager`, or publish archive products. `AssetManager` remains the sole
owner of identity, dependency requests, owned-child binding, transaction
rollback, observations, and unload rules.

Offline importer providers do not depend on `AssetManager`, `AssetID`, Runtime,
Render, or Graphics. Compound source suffixes such as `.model3.json` belong to
the offline importer registry and must not be handled by a generic `.json`
runtime branch.

## Runtime data flow

```text
asset path
  -> Asset type registry resolves native product type
  -> registered loader creates shared IAssetPayload
  -> AssetManager validates payload/type and commits AssetRegisterInfo
  -> cache owns Asset wrapper and shared payload
  -> consumers request GetResource<T>()
```

The existing load lock order and transaction sequence remain authoritative:
provider code runs only in the existing loader-serialization window; registry
lookup does not call provider code; dependency requests are resolved after the
loader lock is released; observations are published after Asset state locks are
released.

## Migration sequence

Each stage must be independently buildable and revertible.

### AX1.0 — Baseline contract characterization (landed 2026-09-08)

- Record all built-in `AssetType` values and representative packed `AssetID`
  values.
- Add or preserve tests for current suffix routing, typed payload access,
  dependency registration, rollback, unload, stale IDs, and observations.
- Inventory every `std::get`, `std::visit`, and payload alternative consumer.

Implementation evidence:

- [`asset_contract_baseline_test.cpp`](../../../engine/test/unit/asset/asset_contract_baseline_test.cpp)
  pins the built-in type values, one packed-ID golden value, built-in suffix
  routing, typed payload access/lifetime, dependency-protected unload, and
  invalid owned-child rollback.
- Existing Asset tests cover stale IDs, owned-child rollback, loader failures,
  concurrent deduplication, and recursive dependency observations. The AX1.0
  validation run is recorded in the
  [AX1.0 journal](../../../.spec/journal/2026-09-08-asset-ax1.md).

Recorded synchronization and observation baseline:

| Area | Current contract |
| --- | --- |
| Shared loader state | `load_mutex_` serializes shared loader access. |
| Asset cache state | `state_mutex_` guards caches, slots, path indexes, and dependency/referrer mutation. |
| Lock order | Loader work is serialized first; dependency loads occur after the loader lock is released; registration then takes Asset state lock. |
| Observation state | Observation uses its own state and is updated after Asset/load locks where the load path publishes timing and terminal state. |
| Successful load phases | Cache lookup → waiting for loader → source load → dependency resolution → registration → succeeded. |
| Failed load phases | Failure is terminal at the phase that rejects the extension, source, dependency, or registration. |

Gate: the baseline is documented and tests pass before the payload migration.

### AX1.1 — Polymorphic payload core (landed 2026-09-08)

- Add `IAssetPayload` with a virtual destructor and `GetAssetType()`.
- Convert built-in resource structs to implement the interface.
- Change `AssetPayload` to `std::shared_ptr<IAssetPayload>`.
- Implement `IsValidResource` and `Asset::GetResource<T>()` with null checks and
  `std::dynamic_pointer_cast<T>`.
- Preserve shared lifetime when the `Asset` wrapper is released.

Gate: built-in loaders and all existing resource consumers pass without
feature-specific branches. Wrong-type and null-payload tests are explicit.

Implementation evidence:

- [`asset_payload.h`](../../../engine/runtime/asset/asset_payload.h) owns the
  polymorphic `IAssetPayload` contract and `AssetPayload` alias.
- All built-in CPU resources implement `GetAssetType()` and retain their
  existing built-in type values.
- [`asset.h`](../../../engine/runtime/asset/asset.h) stores one shared
  polymorphic payload and implements typed access with
  `std::dynamic_pointer_cast<T>`.
- Asset registration, decoded-size accounting, native model loading, examples,
  and tests were migrated from top-level payload-variant access to typed casts.
- The AX1.1 validation evidence is recorded in the
  [AX1.1 journal](../../../.spec/journal/2026-09-08-asset-ax1-1.md).

AX1.2 remains responsible for the generic type/loader registry. It must define
the type-value policy before adding more `AssetType` values:

- Add a built-in `AssetType` constant only when the asset core itself owns the
  common identity, native suffix, loader contract, and baseline runtime
  behavior. Built-in values are append-only and must preserve the existing
  numeric assignments.
- Reserve a documented custom range for module-owned types. A feature module
  registers a stable value from that range through the Asset registry instead
  of editing the common built-in enum for every extension.
- The registry validates that the registered descriptor, `AssetRegisterInfo::type`,
  `AssetID::type`, and `IAssetPayload::GetAssetType()` agree before publication.

### AX1.2 — Generic type and loader registry (landed 2026-09-08)

- Add an Asset-owned descriptor/registry with explicit registration and seal.
- Route native suffixes through descriptors while keeping built-in numeric IDs
  and path/cache behavior unchanged.
- Adapt existing built-in loaders behind the generic contract; do not make
  optional modules edit `AssetManager` source.
- Validate descriptor type, `AssetRegisterInfo::type`, and
  `payload->GetAssetType()` before cache mutation.

Gate: built-in and test extension loads share the same cache, dependency,
observation, rollback, concurrency, and unload transaction.

Implementation evidence:

- [`asset_type_registry.h`](../../../engine/runtime/asset/asset_type_registry.h)
  and its implementation provide normalized suffix lookup, type/name/suffix
  collision checks, explicit registration, and a one-way seal.
- [`common.h`](../../../engine/runtime/asset/common.h) preserves the existing
  built-in values and exposes `0x1000..0xEFFF` as the custom module range.
  Values outside the exact built-in set and that custom range are rejected.
- `AssetManager` registers the existing loaders as descriptors, seals the
  registry on the first load, dispatches through the registered callback, and
  validates descriptor type, `AssetRegisterInfo::type`, and payload type before
  cache publication.
- `AssetContractBaselineTest` loads a test-only custom payload through the
  ordinary Asset cache and verifies collision and late-registration behavior.
- The AX1.2 validation evidence is recorded in the
  [AX1.2 journal](../../../.spec/journal/2026-09-08-asset-ax1-2.md).

### AX1.3 — Offline importer registry

- `AssetImportRegistry` now owns a database-free provider descriptor contract:
  stable provider ID/version, provider kind, source suffixes, and a callback
  returning a polymorphic `IImportProduct`. The registry target has no
  `AssetRuntime`, SQLite, or archive dependency.
- Automatic selection normalizes source suffixes case-insensitively and chooses
  the longest matching suffix, so `model3.json` wins over `json`. Equal-length
  matches are reported as ambiguous. An explicit provider ID bypasses suffix
  selection and is useful for importer overrides.
- Existing typed model and texture services are connected through adapters.
  Model import retains its typed request/result and progress callback; texture
  import/cook retains its typed result and publishes through AssetImport.
  Material conversion remains a model-stage operation because its input is an
  `ImportedModelDocument`, not a standalone source-path request/result shape.
- Product publication and archive transactions stay in AssetImport services.
  Runtime only consumes products and never invokes the offline registry.

Gate: the tool works without AssetRuntime, and runtime loading remains read-only
with respect to the archive.

Implementation evidence:

- [`asset_import_registry.h`](../../../engine/runtime/asset/asset_import_registry.h)
  defines the standalone provider contract and typed polymorphic result
  wrapper; [`asset_import_registry.cpp`](../../../engine/runtime/asset/asset_import_registry.cpp)
  implements registration, sealing, explicit lookup, and suffix resolution.
- [`asset_import_adapters.h`](../../../engine/runtime/asset/asset_import_adapters.h)
  adapts `ModelImportService` and the database-free texture pipeline without
  adding feature-specific branches to AssetRuntime.
- `KimPeanutAssetTool` now composes the registry for `import` and
  `cook-texture`; the registry test links only `AssetImportRegistry`, proving
  the contract is independently buildable without AssetRuntime or the archive.
- Validation evidence is recorded in the
  [AX1.3 journal](../../../.spec/journal/2026-09-08-asset-ax1-3.md).

### AX1.4 — Extension hardening and consumer handoff (landed 2026-09-08)

- Added an isolated `AssetExtensionHardeningTest` target covering a valid
  external type, malformed payload rejection, throwing-loader rollback,
  dependency-failure rollback, concurrent same-path deduplication, serialized
  loader access, unload/stale-handle behavior, and direct registration type
  validation.
- Confirmed the lock graph remains `load_mutex_` → `state_mutex_`: registered
  callbacks run under the existing shared-loader serialization window, while
  dependency requests are resolved after that window and before publication.
- Corrected the observed `LoadAsync(path, session)` entry point to resolve its
  type through the generic registry, so custom extensions report their actual
  type instead of the legacy built-in suffix switch.
- Documented `AssetManager::RegisterAssetType`, `AssetTypeDescriptor`, the
  custom value range, registration timing, callback obligations, and the
  built-in registration rule. Live2D receives this generic contract in its
  consumer stage; Asset contains no Live2D-specific behavior.

Public registration contract:

```cpp
AssetTypeDescriptor descriptor{};
descriptor.type = module_type_from_custom_range;
descriptor.name = "ModuleAsset";
descriptor.extensions = {"module_asset"};
descriptor.loader = [](const std::string &path, AssetRegisterInfo &info)
{
    info.path = path;
    info.name = "ModuleAssetPayload";
    info.type = module_type_from_custom_range;
    info.resource = std::make_shared<ModulePayload>();
    return true;
};

std::string diagnostic;
AssetManager::GetInstance().RegisterAssetType(
    std::move(descriptor), diagnostic);
```

The module must choose a stable value in `0x1000..0xEFFF`, use a unique
normalized suffix, and register before the first runtime load. The manager
registers built-in descriptors during construction; modules do not re-register
built-ins. The registry seals on first load, so late registration is rejected.
The callback only fills `AssetRegisterInfo`; it must not allocate `AssetID`s,
mutate caches, publish archive products, or recursively call `AssetManager`.
`info.type`, the payload's `GetAssetType()`, and the descriptor type must agree
or the transaction is rejected without cache publication. Dependencies are
declared through `dependency_requests` and resolved by AssetManager.

AX1.4 deliberately does not promise parallel decoding. `LoadAsync` moves the
same transaction to a worker and concurrent requests for one path converge on
one cache identity; the current shared loader lock keeps callbacks serialized.
Parallel throughput is a later loader-pool/per-thread-instance decision.

Implementation evidence:

- [`asset_extension_hardening_test.cpp`](../../../engine/test/unit/asset/asset_extension_hardening_test.cpp)
  exercises the generic runtime transaction without Live2D or Asset source
  branches.
- [`asset_manager.cpp`](../../../engine/runtime/asset/asset_manager.cpp)
  resolves custom types for observed asynchronous loads through the same
  registry used by synchronous loads.
- Validation evidence is recorded in the
  [AX1.4 journal](../../../.spec/journal/2026-09-08-asset-ax1-4.md).

Gate: Asset owns the extension mechanism and a fake extension can load through
ordinary Asset transactions before Live2D is enabled.

## Invariants

- Asset core has no optional-module include, type, suffix, or loader branch.
- Existing built-in type values and packed IDs remain byte-for-byte compatible.
- A payload cannot be registered under a different type than its own
  `GetAssetType()` result.
- Payload shared ownership never transfers GPU ownership into Asset.
- Provider callbacks never execute while the registry or Asset state lock is
  held.
- Dependency loads occur after the shared loader lock is released.
- Failed loads publish no partial cache entry, path index entry, dependency
  edge, owned child, or observation success.
- Static module registration is explicit; global constructors and hot-unload
  are outside this plan.

## Acceptance criteria

- [x] All built-in payloads implement `IAssetPayload`.
- [x] Top-level `AssetPayload` is `std::shared_ptr<IAssetPayload>`; no central
  resource variant or visitor remains in Asset.
- [x] `Asset::GetResource<T>()` uses `std::dynamic_pointer_cast<T>` and returns
  a shared reference with the documented lifetime.
- [x] Built-in values, packed IDs, suffix routing, and existing load semantics
  remain compatible.
- [x] A test-only external payload loads through the same cache and transaction
  path as built-in assets.
- [x] The Asset extension registry is generic and contains no Live2D knowledge.
- [x] Runtime and offline importer contracts are independently testable.
- [ ] Live2D can register its payload and loader without editing Asset source.

## Validation plan

Documentation work does not require a build. When AX1 implementation begins,
use the Asset validation matrix and at minimum run:

```powershell
.\tools\kp.ps1 build AssetUnitTest
.\tools\kp.ps1 test Asset
cmake --build build --config Debug --target KimPeanutAssetTool
cmake --build build --config Debug
ctest --test-dir build -C Debug
```

Add focused tests for payload lifetime, wrong-type access, extension collision,
transaction rollback, concurrent same-path loading, and Live2D-off composition.

## Reference notes

Godot's resource boundary is a useful precedent for a common reference-counted
resource base and loader registration, but its object database, threaded loader,
and scripting/runtime class system are not adopted here:

- [Godot Resource](https://github.com/godotengine/godot-docs/blob/master/classes/class_resource.rst)
- [Godot ResourceFormatLoader](https://github.com/godotengine/godot/blob/master/core/io/resource_loader.h)

The applicable conclusion is limited: Asset needs one stable polymorphic
resource boundary and explicit loader composition. Asset identity, locking,
transaction rollback, and native product policy remain KimPeanutEngine-owned.
