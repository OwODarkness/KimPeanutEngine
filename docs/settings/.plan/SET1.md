# SET1 — Headless settings service

**Status: proposed.** Parent: [roadmap](../TODO.md).
Ownership and policies are defined once in [PLANS.md](../PLANS.md).

## Boundary and deliverable

Deliver RuntimeSettings and focused tests with a fake domain provider. No
renderer, audio device, settings window, Engine preference migration, or actual
project-settings file is introduced in this stage. Temporary test directories
prove persistence without changing the user's configuration.

## Planned files

- `engine/runtime/settings/settings_types.h`: neutral data contracts using
  ReflectionValue/ReflectionPropertyMetadata; enum scope and application policy.
- `settings_registry.h/.cpp`: catalog, layers, requests, snapshots and lifetime.
- `settings_store.h/.cpp`: versioned JSON and safe file operations.
- `i_settings_provider.h`: optional provider interface accepting immutable
  requests and reporting results; no implied inheritance for domain systems.
- `CMakeLists.txt`: standalone RuntimeSettings target, Reflection vocabulary and
  private JSON dependency, no RuntimeLib/Editor/Render/Graphics linkage.
- `engine/test/unit/settings/`: SettingsUnitTest; add its directory to the
  existing unit-test root. Use the repository GoogleTest conventions.

## Contract decisions

Register schema independently of a live binding. A section includes canonical
key, label/category, schema version, property metadata/defaults, allowed save
scopes, application policy, and pure validation/conversion callbacks. Functions
are domain-owned but may only inspect copied configuration. The registry checks
structural/type constraints; it must not encode domain field names.

Return diagnostics for duplicate keys, unsupported property types, invalid
defaults, out-of-range/nonfinite values and illegal scope. Seal before load;
registrations after seal fail. No startup-order-dependent global registration.

Read APIs return copied catalog/snapshots. Request APIs accept section key,
base revision, changed values and origin. Build a complete candidate from the
latest accepted section, validate once, then queue atomically. One section may
have only one in-flight apply; return Busy for further submissions until it
resolves, while allowing drafts to change locally. Also bound the global queue
and completion history. Delayed acknowledgements must match binding generation
and request ID. Superseded is explicit if a domain's existing queue replaces a
request through another internal path.

Separate registration identity, request identity and section value revision.
Token release invalidates the provider, cancels unresolved requests and causes
late completion to be ignored. A copied snapshot remains valid after release.
Reject new edits while shutting down. Do not call validators/providers while
holding registry locks; stabilize their registration lifetime for the call.

Keep the interface small: register/seal schema; load/resolve layers; bind/unbind
provider; snapshot; submit/pump/complete; save/remove overrides; shutdown. Exact
C++ signatures belong to implementation review after these behaviors are tested.

## Store behavior

Use a top-level format version, named sections and per-section versions. Store
overrides rather than a dump of effective runtime state. Preserve unknown JSON
members for optional modules. Disallow saving a layer whose malformed/future
format could not be read safely; retain an actionable diagnostic.

Invalid known section data falls back to the previous valid lower layer as a
whole. Migrations run on copied section data and validate their output before
use. A failed migration leaves the input file unchanged. A restart-required edit
can be saved as desired configuration without changing active state.

Save only accepted desired values in the explicit destination layer and use
same-directory temporary-file replacement. Check file failures, including
replacement failure on Windows, and retain the prior valid file. Detect a
changed-on-disk file since load and report a conflict instead of overwriting it.
No automatic filesystem watching is required.

## Implementation order and acceptance

1. Value/schema contracts and registration tests: duplicates, malformed defaults,
   enum/range/type checks, sealed catalog, no subsystem dependencies.
2. Layer resolution/provenance: missing files, legal scopes, explicit session
   overrides, remove-override, default restoration, unknown data round trips,
   migration and malformed/newer-version failure behavior.
3. Provider lifecycle and edit flow: full-section validation, stale revisions,
   pending versus applied, rejected/failed apply, Busy bounds, cancellation,
   shutdown, provider removal and late completion. A fake provider must reject
   unsupported live changes without corrupting accepted configuration.
4. Persistence: restart-required desired values, explicit save/reopen, failed
   write/replace, disk conflict and unknown optional-module preservation.

Build/test commands below are planned, not current evidence:

```powershell
cmake --build build --config Debug --target SettingsUnitTest ReflectionUnitTest
ctest --test-dir build -C Debug -L 'settings|reflection' --output-on-failure
```

Test discovery must assign the `settings` label. Run the placement evidence
command before edits and final review. Escalate shared Reflection/CMake changes
according to the validation matrix. SET1 acceptance requires no GUI launch.
