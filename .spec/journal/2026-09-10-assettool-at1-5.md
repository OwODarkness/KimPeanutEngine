# AssetTool AT1.5 — Fast no-op import and explicit integrity audit

## Scope

AT1.5 separates ordinary unchanged-source cache reuse from the expensive full
native-product validation pass. The runtime archive ownership and native
loading verification boundary are unchanged.

## Implementation — 2026-09-10

- Added `ModelArchiveDatabase::ProbeSourceFast`, which checks source identity,
  canonical content-addressed product paths, product existence, and recorded
  byte sizes without reading product bytes.
- Kept `ProbeSource` as the strict byte/hash probe for callers that need it.
- Changed `ModelImportService` cache hits to use the fast probe and removed the
  repeated model/material/Texture closure deserialization from unchanged
  imports.
- Kept missing and truncated products on the rebuild path. Same-size product
  replacement is intentionally accepted by the routine metadata trust policy
  and is detected by explicit `ModelArchiveDatabase::IntegrityCheck`.
- Changed archive initialization to run only the database quick check, so a
  damaged product can still trigger an import rebuild instead of preventing the
  archive from opening.
- Expanded `IntegrityCheck` to enumerate every archive product and verify its
  canonical path, size, and SHA-256 bytes. This includes products not currently
  referenced by a source snapshot.
- Added cache-probe telemetry for dependency count, product metadata count, and
  product bytes read; the fast cache-hit contract reports zero product bytes.

The existing AT1 reference gate remains applicable: O3DE's catalog/product
separation supports keeping small source/catalog metadata in the routine path
while reserving product validation for an explicit audit. No new dependency or
runtime archive boundary was adopted.

## Validation

- `cmake --build build --config RelWithDebInfo --target KimPeanutAssetTool ModelArchiveDatabaseTest ModelImportServiceTest` — passed.
- `ctest --test-dir build -C RelWithDebInfo -R "ModelArchiveDatabaseTest|ModelImportServiceTest" --output-on-failure` — 15/15 passed.
- Fast cache-hit regression verifies zero source decode, zero Texture cook,
  zero product validation, zero product bytes read, and visible probe counts.
- Archive regression verifies a same-size replacement remains a fast-probe hit
  but is rejected by explicit `IntegrityCheck`; rebuilds still detect immutable
  product collisions after the source package changes.

## Remaining risk

Routine cache reuse trusts same-size product bytes until an explicit integrity
audit or native runtime verification reads them. AT1.6 still owns the Sponza
three-run no-op timing and end-to-end integration evidence.
