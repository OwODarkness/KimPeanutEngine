# Post-GP7 — Selectable Startup-Level Override

- Status: complete (2026-09-02)
- Roadmap: [Gameplay TODO](../TODO.md#post-gp7--selectable-startup-level-override)
- Depends on: [GP7.5 handoff](GP7.5.md)
- Affected owners: Editor executable entry point, Runtime Engine, Bootstrap,
  Asset
- Unaffected owners: Gameplay level format, Render, Graphics/RHI

## Outcome

Allow a developer, test, or agent to launch one checked-in `.level` fixture
without editing `config/bootstrap.json`:

```powershell
KimPeanutEngine.exe --graphics-api vulkan `
  --startup-level level/point_shadow_validation.level `
  --agent-port 37373
```

The override selects only the initial level. Bootstrap remains the durable
project default, Asset performs the same validation/loading, and Runtime uses
the same startup transaction introduced by GP7.4. The option does not switch a
level after the engine has started.

This follows the useful part of `capture.screenshot`'s design: one typed,
validated selector chooses a known source/view while the owning subsystem keeps
execution and lifetime policy. It is a launch option rather than a Runtime
command because command registration and the local agent transport become
available only after startup-level selection has already committed.

## Local problem

GP7.5 and future renderer verification currently select PBR, point-shadow, and
spot-shadow fixtures by temporarily editing `bootstrap.json`, launching one
backend, capturing, and restoring the file. That is error-prone:

- parallel or interrupted work can leave the repository on a validation level;
- a dirty config obscures whether a capture came from the intended level;
- repeated Vulkan/OpenGL runs require unnecessary source-tree mutation;
- a typo or failed restoration can silently change the normal startup scene.

The engine entry point already parses `--graphics-api` and `--agent-port`, but
the parsing is ad hoc and unknown options are ignored. A misspelled level
selector could therefore launch the default PBR level and produce misleading
evidence.

## Design boundary

```text
bootstrap.json ---------------------+
  default startup_level             |
                                    v
argv -> LaunchOptions -> selection precedence
         optional startup override  |
                                    v
                    shared Asset path validation
                                    |
                                    v
                         Engine::LoadStartupLevel
                                    |
                              ready Level AssetID
                                    |
                         existing Runtime startup
```

- The executable entry point owns argument syntax and user-facing parse
  diagnostics.
- Bootstrap owns the durable default selection.
- Engine owns precedence and passes one selected normalized path to Asset.
- Asset owns root-relative normalization, `.level` typing, CPU loading, and
  dependency closure.
- Runtime receives only the resulting `KPAT_Level` AssetID.
- Gameplay, Render, and Graphics receive no new option/path contract.

## Command-line contract

Add:

```text
--startup-level <Asset-root-relative level/*.level path>
```

Examples:

```text
--startup-level level/pbr_showcase.level
--startup-level level/point_shadow_validation.level
--startup-level level/spot_shadow_validation.level
```

Rules:

- the value is required and must be non-empty;
- normalize with the same Asset-owned helper used by Bootstrap and
  `LevelLoader`;
- require `KPAT_Level` and the normalized `level/` namespace;
- reject absolute, drive-qualified, NUL-containing, root-escaping, and
  wrong-extension values before engine initialization;
- reject duplicate `--startup-level` options instead of silently using one;
- report the invalid option/value to stderr and exit non-zero;
- log the selected normalized path and whether it came from Bootstrap or CLI.

Selection precedence is closed and deterministic:

```text
valid CLI --startup-level > valid Bootstrap V2 startup_level
```

Always parse and validate Bootstrap V2 even when an override is present. The
override changes one value; it does not turn a malformed project configuration
into a valid project. Do not write the override back to Bootstrap.

## Launch-options parser

Move the existing `main.cpp` loop into a small testable Runtime launch-options
parser returning either:

- `RuntimeLaunchOptions` with optional agent port, graphics API, and normalized
  startup-level override; or
- one diagnostic and a non-success parse status.

The parser owns syntax only. It does not touch AssetManager, create Engine, or
mutate global RuntimeContext. The entry point applies the parsed values through
the existing Engine setters plus one new startup-level override setter before
calling `Initialize`.

As part of extracting the parser, reject unknown options and missing values for
all supported launch flags. This is important for automated capture validity:
an unrecognized `--startup-leevl` must not fall through to the default level.
Preserve the current accepted `vulkan`/`opengl` names and agent-port range.

Do not add a generic variant-based configuration registry or reflection layer
for three launch options.

## Engine selection policy

Add an optional normalized startup-level override to Engine's pre-initialize
state. `LoadStartupLevel` must:

1. reject mutation after initialization/loading begins;
2. read and validate Bootstrap V2;
3. choose CLI override when present, otherwise Bootstrap selection;
4. pass `GetAssetDirectory() + selected_relative_path` to AssetManager;
5. require a valid `KPAT_Level` identity;
6. pass only that AssetID to RuntimeContext;
7. include selection source/path in success and failure diagnostics.

Keep `startup_level_loaded_` and terminal `Clear` semantics unchanged. The
override is immutable for one Engine lifetime.

## Explicit non-goal: live level switching

Do not register `level.select`, `level.load`, or a similar Runtime command in
this task. A live command would require new policy for:

- pausing the game/render frame boundary;
- unloading the current level and retiring copied sources;
- loading dependencies without stalling Render;
- controller/camera replacement;
- rollback to the previous active level;
- GPU/resource retirement and command completion timing.

That is a separate transition/streaming design. Reusing the command registry
only because `capture.screenshot` has a typed argument would hide these
lifetime requirements.

## Reference gate

- [Godot command-line documentation](https://github.com/godotengine/godot-docs/blob/master/tutorials/editor/command_line_tutorial.rst)
  exposes `--scene <path>` to override the project's main scene for one launch.
  Adopt the non-persistent, launch-scoped override; do not import Godot's
  reflected scene/resource model.
- [Godot startup source](https://github.com/godotengine/godot/blob/master/main/main.cpp)
  resolves an explicit game path ahead of the configured main scene, then uses
  the same PackedScene load/instantiate path. This supports selecting before
  runtime composition rather than implementing a post-start command.
- [Urho3D command-line guidance](https://github.com/urho3d/Urho3D/blob/master/Docs/GettingStarted.dox)
  uses launch arguments for resource/scene selection and documents precedence
  when both command-line and default sources exist. Adopt explicit precedence
  and diagnostics; reject its broader script/player configuration scope.

These references answer selection timing and precedence. KimPeanutEngine keeps
its Asset-root-relative validation and AssetID-based Runtime boundary.

## Implementation stages

### SLO.1 — Parse and validate launch options

- introduce the pure `RuntimeLaunchOptions` parser/result;
- move `--graphics-api` and `--agent-port` parsing out of `main.cpp`;
- add `--startup-level` validation through the shared Asset path utility;
- reject unknown, duplicate, missing, and malformed options;
- add focused parser tests without constructing Engine.

### SLO.2 — Apply startup selection precedence

- add an Engine pre-initialize startup-level override setter;
- read Bootstrap and choose CLI-over-default deterministically;
- log source/path and retain the existing AssetID-only Runtime handoff;
- test default, override, invalid state, load failure, and non-persistence.

### SLO.3 — Runtime and documentation proof

- launch PBR, point, and spot levels through the option without modifying
  Bootstrap;
- combine the option with both graphics APIs and `--agent-port`;
- capture at least one identifying view from each fixture;
- verify `git diff -- config/bootstrap.json` remains empty;
- document the option in Runtime/command usage and the agent capture workflow.

## Test matrix

### Parser

- no arguments uses existing defaults and no level override;
- valid options work in any order;
- missing values, unknown flags, invalid API, invalid port, and duplicate flags
  fail with the offending option in the diagnostic;
- normalized `level/./x.level` becomes `level/x.level`;
- empty, absolute, drive-qualified, escaping, non-`level/`, and wrong-type
  startup paths fail before Engine construction.

### Engine selection

- no override loads Bootstrap's PBR default;
- a valid override wins without changing BootstrapConfig or its file;
- malformed Bootstrap still fails even with a valid override;
- a missing/unloadable override reports its normalized path and CLI source;
- setting/changing an override after initialization begins is rejected;
- Runtime receives the selected Level AssetID and no path.

### Integration

- each checked-in level launches via `--startup-level` on Vulkan and OpenGL;
- `--startup-level`, `--graphics-api`, and `--agent-port` compose in one launch;
- capture output visibly matches the selected fixture;
- normal launch with no override remains PBR;
- Bootstrap remains byte-unchanged throughout selection tests.

## Validation

Expected evidence:

1. focused launch-parser, Bootstrap, Asset level-loader, RuntimeStartup, and
   RuntimeLevel tests;
2. affected Runtime target build;
3. full Debug build and complete CTest because the executable startup parser
   and Engine public interface change;
4. Vulkan/OpenGL `GraphicsSmoke`;
5. rebuilt Runtime launch/capture of all three fixtures using only
   `--startup-level`;
6. diff proof that `config/bootstrap.json` was not modified.

Record implementation facts and results in a new dated journal entry or the
appropriate future Gameplay journal; do not append proposed results to the GP7
completion evidence.

## Acceptance criteria

- [x] `--startup-level level/<name>.level` selects one startup level for one
  process launch without modifying Bootstrap.
- [x] Bootstrap remains the validated durable default and CLI has documented
  precedence.
- [x] Invalid or mistyped launch options fail visibly instead of silently
  launching another scene.
- [x] The existing Asset load and Runtime startup transaction are reused; only
  a Level AssetID crosses into RuntimeContext.
- [x] Gameplay, Render, Graphics/RHI, Level V1, and Bootstrap V2 schemas do not
  change.
- [x] No live switching command or transition/streaming behavior is implied.
- [x] Focused/full tests, both backend smokes, and fixture launch/capture proof
  pass with Bootstrap unchanged.
