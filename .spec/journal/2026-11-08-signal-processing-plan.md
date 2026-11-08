# Signal planning — 2026-11-08

## Request and baseline

Plan an educational signal-processing module and simulated oscilloscope,
implemented directly to learn DSP. Numerical correctness precedes CPU/GPU
performance. Canonical name selected as Signal (`signal` directory); proposed
tool mode is `signal-lab`.

HEAD inspected: `8a86bf1b3cc22014704a932e317b1d8603f4ac73`. The pre-existing
working tree contains Render/Graphics code, AGENTS/workflow/evidence docs,
status changes, validation tooling and specs/journals. They were preserved;
only a concise Signal entry is added to the already-modified status file.

## Investigation and decisions

- Read project status, README, workflow, completion evidence, validation
  matrix, spec conventions, Terrain/3DGS/Audio design and engine-reference index.
- Read modular-documentation, engine-reference and ImGui UI/UX skills.
- Inspected Audio Player waveform/FFT implementations, host and mode APIs,
  module/Panel/Terrain CMake wiring, CommandRegistry, Core AsyncQueue, and
  common Graphics recorder/capability surface.
- Studied Godot analyzer header/implementation at
  `c51ff06e8836a70ea905701c081bf94124dcd6ce`; checked FFTW DFT and SciPy
  periodogram/filter definitions. [Reference findings](../../docs/signal/references.md).
- Chose a new headless numerical owner, CPU acquisition/session owner and
  separate tool composition. Reuse engine registration, commands and EditorUI;
  keep Audio Player display helpers local because their clamped output and
  discarded phase are not a calibrated DSP contract. Local plot geometry
  remains a tool concern. No code, public API, resource lifetime or dependency
  changes are made in this planning task.
- Split SG1-SG8 by learning experiment and acceptance. General compute support
  is explicitly deferred to SG7 with a current FIR consumer, not assumed ready.

## Validation evidence

Before editing, the broad placement scan over `engine/module`, Audio editor,
host, Core async and common Graphics found eight existing structural include
violations in `mesh_manager.cpp`, `render_backend.cpp`, `sampler_manager.cpp`
and `texture_manager.cpp` under common Graphics. These are backend includes
already present in untouched source, unrelated to this documentation task.
They are recorded, not repaired or treated as new Signal failures.

A focused pre-edit command passed structural checks and displayed reuse matches:

```powershell
./tools/agent/placement.ps1 -Paths engine/module/module_registration.h,engine/runtime/host/application_host.h,engine/runtime/graphics/backend/common/command_recorder.h -Pattern 'SampleSignal|BuildWaveform|ModuleRegistration|ApplicationMode'
```

Level 0 final checks:

- Python local-link/anchor and whitespace check over the eight new Markdown
  files plus Signal links in status: PASS, 45 local links/anchors checked.
- `git diff --check -- docs/signal docs/status.md .spec/specs/signal-processing-lab.md .spec/journal/2026-11-08-signal-processing-plan.md`:
  PASS. Untracked-file whitespace is covered by the Python check above.
- Final placement invocation includes `docs/signal`, the spec/journal and the
  same three source headers from the focused pre-edit command: structural
  checks PASS. This does not supersede the broad existing violations above.
- Semantic review: sample timing and units, transform/window conventions,
  trigger ownership, immutable publication, mode wiring and safe future GPU
  retirement are explicit. No new dependency or lifetime effect exists now.

C++ builds/tests, GUI captures and performance sampling are intentionally
skipped: this task changes plans only. No numerical or runtime acceptance is
claimed; no source/environment build failure was tested.

## Remaining work

Start [SG1](../../docs/signal/.plan/SG1.md) to implement the first analytic
fixtures, then [SG2](../../docs/signal/.plan/SG2.md) for the visible scope.
All module implementation stages remain proposed. The general RHI compute
gap, live-input transport and hardware calibration remain future work.

## Initial implementation follow-up (2026-11-08)

Added the `signal-lab` host-presented mode scaffold, `SignalCore` interface
target with a planar `SampleBlock` contract, and an empty-state ImGui workspace.
The CMake option remains OFF by default, matching SG1's optional-module plan.
This establishes only the UI and data boundary; generators, analysis,
acquisition, measurements and Signal commands remain unimplemented. Build the
app with `-DKPENGINE_ENABLE_SIGNAL=ON` to include the host registration. The
later host-registry follow-up records build and visible-shell validation.

## Host registry and scaffold validation follow-up (2026-11-08)

Replaced the fixed `ApplicationMode` enum and per-Engine factory slots with
`ApplicationHostRegistry::Global()`. Each linked module registers its own
descriptor by string ID, including factory, presentation layout and gameplay
tick policy. The launch parser accepts syntactically valid IDs; `main` rejects
IDs not registered in the linked build. Adding a host no longer requires a
Runtime enum case or fixed-table update. Existing per-mode command-line option
validation remains where an option targets module-specific content.

Validation: `cmake --build build --config Debug --target KimPeanutEngine`
passed. `signal-lab` started with a fresh agent port; its visible `GLFW30`
window was on the Default desktop and a full-window screenshot was captured at
`save/screenshots/validation/signal-lab-global-registry-2026-11-08.png`. The
window was not verified as foreground. CTest and numerical/DSP acceptance were
not run; no signal generator, acquisition or measurement behavior is present.
`git diff --check` and the final scoped `tools/agent/placement.ps1` structural
check passed. A port collision occurred on the first launch attempt; retrying
on a fresh port resolved it.
