# TE2 — durable dialog library and WAV export

Status: implementation complete; visible runtime acceptance remains open.
Parent: [editor roadmap](../TODO.md),
[TE1](TE1.md).

## Design boundary and exit

The editor owns dialog metadata, imports, and exports. Core TTS may return a
bounded immutable copy of the provider's WAV bytes alongside its Audio handle;
it must not write files or depend on Editor. Runtime Audio continues to own
playback voices. Exit when generated speech survives restart, imported WAVs
can be previewed, rows can be duplicated or deleted, and WAV export uses an
owned artifact with collision-safe atomic publication.

## Storage contract

Store a versioned manifest at `save/tts_editor/library.json` and canonical
PCM16 WAV artifacts under `save/tts_editor/audio/`. Keep only playable,
completed rows in the durable manifest. In-flight, failed, and cancelled jobs
remain session rows; their partial bytes are never published. Save audio first
and the manifest second. Orphan audio after a crash is harmless and may be
cleaned by a later explicit maintenance action. Do not delete files referenced
by another row. Loading malformed or unsafe manifest paths must not escape
the library directory; report the error and retain the file for repair.

Use monotonically increasing row IDs across restarts. A duplicate creates a
new metadata row referencing the same immutable artifact. An import validates
a local WAV, writes an owned canonical artifact, and adds a row. A delete
removes a row and updates the manifest; artifact cleanup is deferred to avoid
breaking duplicates or a crash during publication.

## Audio artifact contract

Capture up to 32 MiB of provider WAV bytes on the synthesis worker, for both
stream and buffer paths. Publish an immutable shared payload only on success.
The editor canonicalizes the validated PCM16 RIFF `fmt` and `data` chunks,
repairing the unspecified streaming length and recording actual payload size.
Generated WAV content is never reconstructed from the Audio ring buffer or
the player handle. Export reads the owned canonical artifact.

## UI and file behavior

Add Import, Duplicate, Delete, and Export actions to the dialog list and
output pane. Import takes a local WAV path. Export uses the configured output
directory and a user-editable basename; reject separators, reserved names,
oversize/invalid files, and unwritable destinations. Never overwrite an
existing WAV: add `-2`, `-3`, and so on. Write a same-directory temporary file,
then publish without replacement. Show the selected row's operation result.
Restored and imported artifacts replay with a buffer player, regardless of
their original synthesis mode, so controls describe the actual player.

## Acceptance and validation

- Settings remain separate and Git-ignored; library lives under ignored
  generated `save/` state.
- Complete output survives a new controller instance and can be previewed.
- Cancelled and failed work leaves no manifest row or partial artifact.
- A large valid WAV reaches the bound; overflow fails without publication.
- Duplicate shares immutable content; delete does not invalidate another row.
- Import rejects malformed WAV and path escape. Export handles collisions and
  atomic failure without modifying prior files.
- Rebuild Debug and run targeted TTS/Audio tests. The user has deferred the
  visible TE1 window check; TE2 runtime UI validation remains pending unless
  separately requested.

Implementation note: focused TTS validation covers storage, WAV
canonicalization, import rejection, duplicate sharing and delete retention,
restart restore, export collisions and publication failure, and generation
cancellation/failure. Runtime validation still needs the visible TTS mode to
exercise dialog operations and buffer preview against the real window.
