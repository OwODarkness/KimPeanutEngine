# RenderSystem R1.4 Ready Asset Ingestion

- Status: implemented
- Owner: project maintainers / implementing agent
- Parent TODO: [Render R1.4](../../docs/render/TODO.md)
- Stage design: [R1.4](../../docs/render/.plan/R1.4.md)
- Predecessor: [R1.3](render-system-r1-3.md)

## Objective

Remove path loading and CPU resource processing from Render. Runtime must build
one immutable, validated, lifetime-pinning catalog from the selected level's
ready dependency closure and the renderer's closed built-in requirements before
`RenderSystem` initializes.

The linked stage design is authoritative for ownership, contracts, built-in
roles, migration details, rejected alternatives, and reference evidence. This
spec records the execution and acceptance contract.

## Scope

- Add a typed immutable prepared Render asset catalog.
- Add a closed role table for five renderer shader programs and two default
  textures.
- Build the catalog transactionally in Runtime through Asset and Resource.
- Process all reachable shader variants and optional selected-level environment
  IBL before publication.
- Inject the catalog into RenderSystem, material resolution,
  RenderResourceResolver, and DeferredRenderer.
- Remove Render-side loading, processing, global AssetManager lookup, the unused
  path queue, request-ID cache, and request polling surface.
- Preserve current GPU cache ownership, fixed passes, materials, environment,
  capture, editor composition, and teardown.

## Non-goals

- General runtime streaming, hot reload, cancellation, priority queues, or
  cross-level eviction.
- Asset schema changes for renderer built-ins.
- GPU resource ownership in Asset or Resource.
- Render graph, pass, shader ABI, visual-feature, or RHI changes.
- R1.5 public-facade hardening.

## Invariants

- Asset owns identity, paths, loading, dependency validation, and CPU asset
  wrapper lifetime.
- Resource owns CPU shader/IBL processing and no GPU handles.
- Render consumes only prepared records and owns render policy/GPU cache keys.
- Graphics owns GPU resources, backend execution, synchronization, and safe
  destruction.
- The catalog is immutable, contains no paths or backend types, and pins every
  published payload.
- Startup catalog publication is all-or-nothing.
- Existing R1.2/R1.3 renderer ownership, pass order, failure behavior, and
  teardown remain intact.

## Stages

1. Characterize all current Render loading/processing call sites and confirm the
   path queue/polling surface has no production caller.
2. Implement the closed built-in table, prepared records, immutable catalog,
   and validation tests.
3. Implement Runtime closure traversal, shader/IBL preparation, diagnostics,
   and transactional publication.
4. Migrate material, mesh, texture, environment, and pass-program consumers to
   the catalog without changing GPU cache behavior.
5. Remove the legacy queue/request cache and audit Render dependencies.
6. Run focused/full tests, dual-backend smoke, and visual captures; record the
   factual journal and update ledgers.

## Acceptance criteria

- [x] Every built-in role is present exactly once with the expected ready type.
- [x] The catalog pins typed payloads and validated dependency order without
  storing paths, Asset wrappers, GPU handles, or backend types.
- [x] All reachable shader variants and optional environment IBL are processed
  for the selected API before Render initialization.
- [x] Render contains no loading, path resolution, global AssetManager lookup,
  shader processing, or environment CPU processing.
- [x] The path queue, `AssetLoadRequest`, request-ID cache, and unused polling
  APIs are removed after caller audit.
- [x] Material fallbacks/dependencies, pass pipelines, environment bindings,
  capture, editor composition, and reverse teardown are behaviorally unchanged.
- [ ] Focused tests, full Debug build/CTest, Vulkan/OpenGL smoke, and inspected
  PBR/point-shadow/spot-shadow captures pass. Focused tests, full build/CTest,
  and all six inspected fixture captures pass; the strict GraphicsSmoke
  cross-backend silhouette comparator remains the sole open evidence item.
- [x] A journal records reference evidence, implementation facts, validation,
  skipped checks, and remaining streaming/eviction risk.

## Validation

Use Levels 2–4 from `docs/validation_matrix.md`:

- focused Asset, Resource, Material, RenderSystem, and RuntimeLevel tests;
- architecture search proving forbidden Render calls and legacy request types
  are absent;
- full Debug build and CTest;
- direct Vulkan and OpenGL GraphicsSmoke;
- fresh command-registry SceneColor captures for PBR, point-shadow, and
  spot-shadow fixtures on both backends.

Do not mark R1.4 complete based only on compilation.
