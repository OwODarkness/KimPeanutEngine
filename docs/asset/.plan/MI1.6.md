# MI1.6 — Transactional Model Importer

- Status: landed 2026-09-06
- Parent design: [MI1](MI1.md)
- Prerequisites: MI1.2, MI1.3, MI1.4, and MI1.5
- Unblocks: MI1.7 and MI1.8

## Assignment

Compose hashing, archive lookup, decoding, conversion, validation, publication,
and metadata commit into one explicit offline import/reimport service. The
service must be callable from a CLI or editor subprocess while the engine
application is closed. Preserve the last successful source root on every
failure.

## Deliverables

- Exact cache-hit path that hashes recorded dependencies, verifies products,
  returns `UpToDate`, and performs no Assimp decode or writes.
- A standalone `ModelImportService`/tool boundary that accepts source paths,
  import settings, and an archive root without constructing `AssetManager`,
  `AssetID`, Runtime, Editor, Render, or Graphics objects.
- Per-source coordination, operation-private staging, standalone product-
  deserializer validation,
  create-if-absent immutable publication, concurrent winner verification, and
  a short final SQLite transaction that updates the source root last.
- Cleanup limited to the current operation's staging area; unreachable published
  products remain safe for later explicit garbage collection.
- Tests for stale dependencies, shared-product deduplication, concurrent import,
  rollback, crash boundaries, missing/corrupt products, collision handling, and
  path escape rejection.

## Boundaries

Do not import inside `AssetManager::LoadSync`, make the importer depend on
`AssetManager` or the running engine application, hold Asset/database locks
during Assimp or serialization, overwrite hash-named files, delete old
products, or make filesystem publication appear atomic with SQLite commit.

## Done when

- [x] A verified repeat import performs no writes and no decode.
- [x] Failure leaves the previous database root and products usable.
- [x] A committed root never refers to a missing or unverified product.
- [x] The importer can create and validate products from a standalone process
      while the engine application is not running.

## Landed implementation

`ModelImportService` is the offline orchestration boundary. It hashes the
recorded source closure before invoking Assimp, returns `UpToDate` after
verified product and reference checks, and otherwise decodes, converts,
serializes, validates, and stages a new product set. Products are published
with create-if-absent hard links into the archive; concurrent winners are
re-hashed rather than overwritten. Only after publication succeeds does the
short SQLite transaction update source/dependency/product relationships.

The service coordinates imports per normalized source within a process and
leaves unreachable immutable products in place after a failed metadata commit.
It uses no AssetManager, AssetID, Runtime, Editor, Render, or Graphics object,
so a CLI/editor subprocess can run it while the engine is closed.
