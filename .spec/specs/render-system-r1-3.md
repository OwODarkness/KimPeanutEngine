# RenderSystem R1.3 Fixed Pass Sequence

- Status: complete
- Owner: project maintainers / implementing agent
- Parent TODO: [Render R1.3](../../docs/render/TODO.md)
- Stage design: [R1.3](../../docs/render/.plan/R1.3.md)
- Predecessor: [R1.2](render-system-r1-2.md)

## Objective

Make one immutable typed fixed-pass sequence drive both logical resource
validation and actual deferred-renderer invocation order, including conditional
capture conversion and the optional external terminal editor pass.

The linked stage design is authoritative for ownership, interfaces, the
canonical pass table, validation rules, rejected alternatives, and reference
evidence. This spec records the implementation and acceptance contract only.

## Current state

- R1.2 predecessor evidence is closed and its review is complete.
- R1.3 uses an immutable typed sequence with canonical ordinal validation and a
  consuming per-frame cursor that invalidates moved-from frames.
- Focused tests, full Debug build, complete CTest, dual-backend smoke, and
  fresh Runtime startup-level captures pass.

## Scope

- Close or explicitly waive the R1.2 predecessor evidence gate.
- Replace the mutable declaration-only schedule with an immutable typed fixed
  sequence and per-frame execution cursor.
- Split directional, spot, and point shadow entries.
- Validate identity, resources, conditions, external ownership, and terminal
  rules before initialization succeeds.
- Execute renderer stages only by visiting canonical entries in order.
- Integrate diagnostic capture gating, optional external editor execution, and
  frame finalization into the cursor.
- Make pass outcomes observable to renderer tests without adding a public
  engine diagnostics API.

## Non-goals

- Render graph algorithms or resource/synchronization automation.
- Dynamic pass registration, pass objects, renderer plugins, or RHI changes.
- New pass-failure behavior, render features, or R1.4 ingestion work.

## Invariants

- `RenderSystem` and `DeferredRenderer` retain the R1.2 ownership split.
- The canonical table is the only source of pass order.
- Existing target, shader, binding, light/shadow, capture, and editor behavior
  remains unchanged.
- Conditional/external skips cannot invalidate a required resource dependency.
- Required pass failure is recorded but remains fail-soft for this stage.
- All declarations and execution types remain backend-neutral.

## Stages

1. Close or document a waiver for outstanding R1.2 evidence.
2. Implement typed immutable sequence creation and static validation.
3. Implement the move-only per-frame cursor and synthetic execution tests.
4. Define the canonical eight-entry table and route renderer operations by ID.
5. Integrate capture conversion, editor terminal execution, finalization, and
   pass-result reporting; remove duplicated validity/once state.
6. Run focused, full, smoke, and visual validation; create the factual R1.3
   journal and close RS-1 only when evidence passes.

If implementation changes the fixed-order policy, adds graph behavior, changes
failure semantics, or broadens module ownership, update and review the stage
design before continuing.

## Acceptance criteria

- [x] R1.2 predecessor evidence is closed; the earlier Windows SDK permission
  failure is retained as historical journal evidence.
- [x] Every fixed pass ID is present exactly once in one immutable table.
- [x] Static validation covers identity, resource, condition, external, and
  terminal errors.
- [x] Synthetic cursor tests prove exact execution/skip/failure behavior.
- [x] `DeferredRenderer::RecordFrame()` contains no separately authored pass
  order.
- [x] Capture conversion and editor terminal behavior match the stage plan.
- [x] No live recorder remains outside the sequence.
- [x] Focused tests, full Debug build, complete CTest, dual-backend smoke, and
  deterministic capture inspection pass.
- [x] A journal records commands/results, skipped evidence, and remaining R1.4
  risks.

## Validation plan

Selected level: Levels 2–4. The stage changes a shared Render contract and all
deferred frame orchestration.

Targeted development checks:

```powershell
.\tools\kp.ps1 build RenderPassScheduleTest
.\tools\kp.ps1 test RenderPassScheduleTest
.\tools\kp.ps1 build RenderSystemTest
.\tools\kp.ps1 test RenderSystemTest
```

Completion checks:

```powershell
cmake --build build --config Debug
ctest --test-dir build -C Debug --output-on-failure
.\tools\kp.ps1 smoke
```

Then capture and inspect SceneColor and the relevant directional/spot/point
diagnostic views from the checked-in fixtures on Vulkan and OpenGL through the
Runtime command transport.

## Risks and open questions

- R1.4 must preserve the closed sequence/cursor boundary; dynamic pass
  registration and a second recorder order remain out of scope.
- Capture declarations are a conservative union; they must not be mistaken for
  per-view barrier or lifetime data.
- Converting void recorders to outcomes can accidentally change early-return or
  target-end behavior; backend probe and visual evidence must guard it.
- The current unreachable G-buffer debug recorder must be re-audited before it
  is removed or assigned a real conditional entry.
