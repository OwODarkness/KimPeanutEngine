# MI1.4 — Native Model V1

- Status: landed 2026-09-06
- Parent design: [MI1](MI1.md)
- Prerequisites: MI1.2 and MI1.3
- Unblocks: MI1.6 and MI1.7

## Assignment

Define and implement the deterministic, versioned `.model` container and its
read-only Asset loader. A Model owns geometry plus an ordered table of typed
Material references; Mesh sections retain material-slot indices.

## Deliverables

- Canonical little-endian serialization with magic/version/features, vertex and
  index payloads, section table, bounds, Material references, chunk bounds, and
  an integrity digest.
- Defensive parsing with overflow, count, offset, allocation, enum, digest, and
  unsupported-version checks before allocation or registration.
- `NativeModelLoader` dependency requests for referenced Materials, resolved by
  `AssetManager` outside its load mutex.
- Golden-byte, round-trip, malformed-input, determinism, and dependency tests.

## Boundaries

Do not import foreign sources, query SQLite, generate Materials, mutate the
archive, or add GPU ownership. Never serialize raw C++ object memory or rely on
unordered iteration. Coordinate the Material-reference representation with
MI1.5 before freezing V1 bytes.

## Done when

- [x] Equal imported geometry and references serialize to identical bytes.
- [x] The product hash verifies those bytes and determines the archive name.
- [x] Runtime loading declares correct Material dependency edges with no writes.
