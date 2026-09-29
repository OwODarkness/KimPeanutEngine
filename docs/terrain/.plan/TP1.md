# TP1 — headless data and evaluation

## Goal and boundary

Create the first optional terrain targets as CPU-only libraries. `TerrainCore`
owns the finite heightfield domain, immutable scalar fields, typed values and
recipe value model. `TerrainGeneration` owns operator descriptors, recipe
validation, deterministic DAG evaluation and bounded job/result ownership.
There is no Asset, Render, Graphics, Runtime or Editor dependency in either
target. No terrain host or preview publication is part of TP1.

TP1 freezes the first domain convention: a finite rectangular vertex grid;
sample `(x,z)` is at `origin_x + x * spacing_x`, `origin_z + z * spacing_z`;
spacing and elevation are meters; elevation is positive engine Y. Scalar maps
use the same vertex-grid dimensions unless a later operator explicitly declares
a cell-centered grid. Width and height are at least two; spacing must be finite
and positive; origin, datum and samples must be finite. This is a terrain module
convention, not a change to Core's global axis or unit policy.

## Data and ownership

- `GridDomain2D` stores width, height, horizontal origin, per-axis spacing,
  vertical datum and explicit meter units. Checked element-count calculation
  rejects overflow and caller-provided sample budgets are enforced at creation.
- `ScalarField2D` is created through a validating factory and then exposes only
  const metadata/sample access. Evaluated values are `shared_ptr<const ...>`;
  operators build private output buffers and publish only after full success.
- `TerrainRecipe` is a versioned value containing seed, domain, stable node IDs,
  operator IDs/versions, JSON parameters and explicit input connections. JSON
  parsing rejects malformed required structure while retaining extra operator
  parameter keys for forward-compatible editing.
- `OperatorRegistry` is an explicit mutable builder during setup, then shared
  read-only with evaluations. Descriptors declare stable IDs, versions, typed
  input/output ports, and a factory/callback. Duplicate IDs and bad versions
  fail registration. Evaluator behavior does not switch on built-in IDs.
- Evaluation compiles and validates all referenced nodes, required/unknown
  ports, port types, missing sources and cycles before invoking operators.
  Stable topological order is by node ID among ready nodes. Per-node RNG derives
  from recipe seed, stable node ID and operator version; reductions in TP1 are
  serial. Same-profile CPU results are bitwise repeatable.
- `GenerationExecutor` owns a fixed worker set, bounded pending and completion
  queues, revision cancellation and join-on-destruction. Workers capture
  immutable recipe/registry values and return immutable results or diagnostics;
  they never call UI, AssetManager or graphics. A newer revision cancels older
  queued/running work cooperatively. Recipe node count, per-evaluation retained
  output bytes, queue lengths and cache bytes all have explicit limits. Operator
  callbacks must poll cancellation during long work. Shutdown cancels, joins,
  and drains.

## Initial operators and extensibility proof

Register `terrain.scalar.constant` v1 as the smallest source operator. It
emits one scalar field over the recipe domain. Tests register a separate
`test.scalar.offset` operator using only the public descriptor/callback API;
the evaluator must require no edit for it. Its input edge ensures the engine's
dependency hashes and cache invalidation remain node-local. TP1 caches only
completed immutable node outputs, keyed by recipe/domain/seed/operator version,
canonical parameters and input hashes; cache entries and total bytes are
strictly bounded. Cancellation cannot publish a partial cache entry.

## Recipe format

JSON root keys are `schema_version`, `seed`, `domain`, and `nodes`. Domain keys
are `width`, `height`, `origin_x_m`, `origin_z_m`, `spacing_x_m`,
`spacing_z_m`, and `datum_y_m`. Each node has `id`, `operator`, `version`,
`parameters`, and an `inputs` object mapping input port names to
`{"node": id, "port": name}`. Node IDs and port names are stable strings.
Serialization uses canonical node ordering; it preserves operator parameter
objects as data, while evaluation rejects unknown operator IDs/versions.

## Validation and acceptance

Add `KPENGINE_ENABLE_TERRAIN` (default OFF), CPU-only `TerrainCore` and
`TerrainGeneration` targets, and a `TerrainGenerationTest` executable that can
configure/build/run with the option enabled and no Editor/graphics targets.
The focused contract suite covers rectangular/invalid/overflow domains, finite
values, units, JSON round-trip and malformed data, incompatible/missing ports,
cycles, stable same-profile outputs, worker-count-independent job results,
revision cancellation, queue/cache bounds and clean executor shutdown. An
independently registered offset node confirms only dependent nodes recalculate.
Then configure/build once with terrain disabled to verify normal engine wiring
stays unchanged.

Use a small 5x3 asymmetric scalar fixture and a fixed seed as the deterministic
contract fixture. The TP1 result is CPU-only: no screenshot, runtime mode,
physical erosion claim or cross-platform bitwise claim is made.
