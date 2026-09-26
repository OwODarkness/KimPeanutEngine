# Sponza startup bottleneck investigation

Date: 2026-09-26. Scope: measured diagnosis and resolver verification correction.

The first aggregate-log review located the long interval in Asset loading but
did not establish the operation responsible. Follow-up temporary logging exposed
the sealed Asset observation and native model read/hash/decode stages. Two
Debug Vulkan Sponza runs measured 60.380 and 91.000 seconds of Asset loading.
The second run spent 26.410 seconds resolving model references (including
full-file SHA-256 verification), 50.012 seconds computing native model hash
pairs, 0.802 seconds reading model files and 13.656 seconds decoding models.
The serialized model dependency chain dominates the root's dependency wait.

The canonical evidence, operation relationships, timing table and commit
history are in the
[R4.6 measured review](../../docs/render/.review/R4.6.md#measured-sponza-startup-bottleneck-and-history--2026-09-26).
Local logs use the `save/logs/sponza-startup-profile-20260926-{a,b}` prefixes;
generated logs and captures remain outside commits.

Both diagnostic builds passed through `tools/kp.ps1 build KimPeanutEngine`.
Run A initialized and closed gracefully. Run B completed Asset loading but
remained in renderer promotion; a graceful close timed out, then only the
owned diagnostic process was terminated. No teardown success is claimed for B.
Removed all probes from Engine, LevelLoader and NativeModelLoader, verified
those source files have no diff, and rebuilt the normal executable. No loader
policy, verification guarantee, shader, asset or ownership change was applied.
No test suite was rerun for these removed timing probes.

History establishes synchronous model verification since `065c0af` (September
6), internal loader hash reuse since `baeef78` (September 9), and background
full verification for streamed textures since `a7d4e1d` (September 14).
The model calls also exist in HEAD before current RT edits. The bottleneck is
measured; the historical reason that a previous same-code startup ran near
30 seconds is not established by logs without per-operation timing or machine
conditions. At diagnosis time, no performance correction was claimed.

## Verification correction

After reviewing the measured path, `ModelArchiveDatabase::ResolveModelProductPath`
was changed to verify the canonical hash-named path and recorded byte size
without hashing the full Model. The native Model loader still computes and
checks the archive content hash and embedded digest before structural decode
and Asset publication. The shared metadata helper is also used by fast archive
probes and full file verification, so they retain the same path/existence/size
checks.

Validation and runtime evidence:

- `tools/kp.ps1 build KimPeanutEngine` — passed (Debug).
- Fresh Vulkan Sponza startup — 127 Assets, 196 load operations, 42 cache hits;
  Asset loading completed in 46.018 seconds. See
  `save/logs/2026-09-26/KimPeanutEngineLog-2026.09.26-21.01.51.txt`.
- This is 44.982 seconds below the earlier 91.000-second diagnostic run, but
  exceeds the isolated 26.410-second resolver cost. The 30.807-, 60.380-, and
  91.000-second runs show enough spread that this single post-change run cannot
  establish a controlled speedup. Repeated runs under the same build and cache
  conditions are still needed.
- No tests were added or run. The engine reached its Runtime command listener
  and was left open on Sponza. No commit was created.

The history review remains: `065c0af` introduced the blocking resolver check;
`baeef78` removed redundant loader hash recalculation by sharing the content
and embedded-digest computations. The current correction removes the resolver
hash without changing the native Model product format or its strict load-time
integrity checks.
