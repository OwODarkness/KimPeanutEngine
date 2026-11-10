# Agent implementation workflow

This workflow is shared by agents through [AGENTS.md](../AGENTS.md).
Use it for implementation and refactoring across the repository. Scale the
record to the change: four sentences in a progress note suffice for a small
task; reuse the existing spec for larger work.

## Before editing: name the operation and find its owner

1. State the concrete operation and acceptance condition independently of the
   file that currently contains its caller.
2. Search by behavior and by related types. Inspect the implementation,
   consumers and lifetime of likely candidates; filename matches alone are
   insufficient. Use `rg` and the evidence command below.
3. Choose a placement: reuse as-is, extend the existing domain owner, retain a
   local helper, or introduce a focused owner. Record why the other plausible
   candidate does not fit. No new abstraction is required just for this step.
4. Check what knowledge crosses the boundary: data, ownership, lifetime,
   threading, dependencies and failure/submission behavior.

Use this compact record in the task's existing note/spec:

```text
Operation and acceptance:
Existing candidates inspected (paths/symbols and search):
Placement and reason (reuse / extend / local / new owner):
Consumers, ownership/lifetime, and validation:
```

For example, scene-point projection extends RenderCamera beside BuildWorldRay;
DeferredRenderer selects the camera and delegates. Texture footprint reporting
reuses Core format sizing, obtains native allocation sizes through RHI, and lets
target/history owners describe their own attachments. A pass recorder consumes
those values without knowing another feature's attachment layout.

## During implementation: preserve a useful boundary

- An owner holds the state and invariants of its operation. Move associated
  cleanup, invalidation and failure handling with the operation.
- Composition selects owners, gathers their outputs and schedules work. Pass
  resource layouts, camera algorithms and statistics aggregation belong with
  their domain operations.
- Use immutable values or frame-bounded views between owners. Keep GPU native
  details below RHI and retain a single documented resource owner.
- Prefer a named concrete operation over a generic utility namespace, callback
  framework or writable context containing unrelated owners' state.
- Keep a helper local when it has one local purpose and uses only that owner's
  knowledge. Extract when real consumers share the operation, or a distinct
  lifetime/invariant justifies an owner even with one consumer.

## Evidence command

From the repository root:

```powershell
./tools/agent/placement.ps1 -Paths engine/runtime/render -Pattern 'BuildWorldRay|ProjectScenePoint'
./tools/agent/placement.ps1 -Pattern 'GetTextureMipByteCount|Footprint'
```

With explicit paths, the command scans those source files/directories. Without
paths, it includes tracked changes against HEAD (staged and unstaged) and
untracked source files. It prints scoped files, relevant agent guides, and
repository-wide symbol matches. It checks source includes against a small set
of existing architecture rules: new Runtime→Editor includes, native/backend/UI
includes in common Graphics or Render, and a concrete DeferredRenderer include
in the public RenderSystem header. A violation exits nonzero; missing paths,
invalid patterns and Git/search errors also fail. Existing Runtime→Editor
includes are grandfathered, not offered as precedent for expansion.

This is a read-only evidence aid. It cannot prove algorithm equivalence,
appropriate reuse, resource lifetime or semantic ownership. It installs no
hooks and changes no agent-specific configuration.

## Before completion

Run the command again and inspect the actual diff. Confirm the chosen owner has
its state and cleanup, callers reuse the operation, public headers expose only
needed contracts, and failure paths preserve lifetime/submission invariants.
Then follow the [validation matrix](validation_matrix.md). The completion report
names reuse decisions and any justified local helpers alongside test evidence.
