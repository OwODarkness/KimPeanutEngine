---
description: Build and prepare a named example/executable for running
argument-hint: [example-name]
---

Build the executable requested by `$ARGUMENTS` (default: `KimPeanutEngine`):

1. `cmake --build build --config Debug`.
2. Locate the produced `.exe` under `build/` (check the target's output dir) and report its path.
3. Do **not** launch it yourself — the engine examples open windows and block (`while(1)` loops, `main.cpp` selects examples by uncommenting), so launching from a shell will hang it. Hand the path to the user to run.

If `$ARGUMENTS` names an example function (e.g. `ShaderProgramLoad`), note that it is currently called from `engine/editor/main.cpp` by uncommenting — the user must toggle that and rebuild before running.

**Checking logs after a run.** The engine writes one log file per run to `logs/<YYYY-MM-DD>/KimPeanutEngineLog-<YYYY.MM.DD-HH.MM.SS>.txt` (path from `GetLogDirectory()`, see `engine/runtime/core/config/path.h`). To inspect a run, read the **most recently modified** `KimPeanutEngineLog-*.txt` under `logs/` (today's date dir; sort by mtime descending) — the logger flushes to disk periodically while running and on exit, so a run the user just terminated has its full output in its file. Do not rely on capturing console output of a process the user ran.
