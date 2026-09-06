# MI1.2 — Hashing and SQLite Archive Core

- Status: landed 2026-09-06
- Parent design: [MI1](MI1.md)
- Prerequisites: MI1.1
- Unblocks: MI1.4, MI1.5, and MI1.6

## Assignment

Implement the infrastructure that can identify a source package and query or
update its import metadata without decoding a model. Integrate SQLite through a
dedicated dependency target and hide it behind an Asset-owned repository API.

## Deliverables

- Stable SHA-256 values, canonical Asset-relative paths, ordered source-closure
  fingerprints, import keys, and content-addressed product path helpers.
- A narrow `ModelArchiveDatabase` boundary with schema creation, `user_version`
  migrations, foreign keys, prepared statements, checked transactions, WAL,
  bounded busy handling, and stable diagnostics.
- The MI1 tables and constraints defined by the parent plan, plus repository
  operations for lookup, integrity state, relationship replacement, and source
  root publication.
- In-memory database tests and filesystem tests for hash vectors, migration,
  corruption/newer-schema rejection, rollback, and exact no-op decisions.

## Boundaries

Do not invoke Assimp, serialize Model/Material products, expose `sqlite3*`, or
make Runtime depend on SQLite. Keep file hashing and other long work outside
database transactions. Do not reinterpret existing generated products.

## Done when

- [x] A caller can distinguish `UpToDate`, stale input/settings/schema, missing
  product, corrupt product, busy database, and invalid database states.
- [x] Database commits are short and never reference an absent product.
- [x] SQLite is linked only through the intended engine dependency boundary.
