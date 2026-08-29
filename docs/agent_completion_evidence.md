# Agent Completion Evidence

This document defines the standard completion report for implementation tasks in KimPeanutEngine. A task is not complete merely because code was edited or one target compiled; the report must show what changed, what was validated, and what remains uncertain.

## Required report

Every implementation task should provide these sections:

### Task

State the requested outcome in one or two sentences.

### Changed areas

List the affected subsystems and explain why they were in scope.

```text
Changed areas:
- Render
- Graphics/RHI
- Unit tests
```

### Changed files

List the files changed by the task. Group closely related files when the list is long, but do not hide significant changes.

```text
Changed files:
- engine/runtime/render/render_scene.cpp
- engine/runtime/render/render_scene.h
- engine/test/unit/render/render_world_test.cpp
```

### Architecture and ownership impact

Explain the important design consequences:

- Which module owns the new state or resource?
- Did a dependency direction change?
- Did a public API, handle, lifetime, thread, or backend boundary change?
- Which invariants remain true?

If there is no architectural impact, say so explicitly.

```text
Architecture impact:
- RenderScene records through the common CommandRecorder.
- No Vulkan types were added to the Render module.
- Frame-local bindings remain owned by FrameContext.
```

### Validation level

Select the level from [the validation matrix](validation_matrix.md) and explain the choice.

```text
Validation level: Level 3
Reason: the change affects backend command execution and needs runtime smoke evidence.
```

### Commands and results

Record the actual commands run and their result. Do not report a test as passing if it was not executed.

```text
Commands:
cmake --build build --config Debug --target RenderPassScheduleTest
ctest --test-dir build -C Debug -R RenderPassScheduleTest
cmake --build build --config Debug --target GraphicsSmoke
GraphicsSmoke.exe

Results:
- RenderPassScheduleTest: PASS
- GraphicsSmoke: PASS
```

For a failure, include the first relevant error, test name, or executable exit code. Do not bury the original failure under later retry output.

### Skipped validation

List checks that the matrix would normally suggest but were not run, with a reason.

```text
Skipped:
- Full build: not required; no public cross-module API changed.
- Screenshot validation: unavailable because the current smoke target has no capture path.
```

An empty section is valid:

```text
Skipped: none
```

### Blockers and unverified paths

Separate source failures from environment limitations.

```text
Blockers:
- Vulkan runtime smoke not run; the machine has no available Vulkan device.

Unverified:
- Visual output was not compared against a baseline.
```

If compilation is blocked by permissions, missing SDK files, or unavailable third-party dependencies, report that as an environment blocker rather than claiming a source failure.

### Documentation updates

List durable documentation updated by the task, or state that no documentation change was needed.

```text
Documentation:
- Updated docs/status.md.
- Updated docs/render/overview.md.
```

## Copyable template

```text
Task:

Changed areas:
-

Changed files:
-

Architecture impact:
-

Validation level:
Reason:

Commands:

Results:
-

Skipped:
- None

Blockers:
- None

Unverified:
- None

Documentation:
- None
```

## Completion criteria

The agent may claim completion only when:

1. The requested behavior is implemented or the task is explicitly blocked.
2. The selected validation level has been run, or every skipped check is explained.
3. Failures and environment limitations are reported accurately.
4. Ownership and dependency effects have been reviewed.
5. Required status or design documentation has been updated.
