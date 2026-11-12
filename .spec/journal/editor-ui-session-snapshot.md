# Editor UI session snapshots

- Status: partial
- Date: 2026-11-11
- Spec: [execution spec](../specs/editor-ui-session-snapshot.md)
- Parent TODO: [Agent-readable GUI snapshots](../../docs/editor/TODO.md#deferred-follow-up)

## What was done
- Added a bounded, thread-safe snapshot mailbox to the neutral Editor UI API.
- `EditorUiHost` latches requests at frame start and publishes copied observations,
  theme declarations and workspace state only after successful UI submission.
- Added semantic parent tracking for windows, tabs, tables, rows, text, controls,
  Canvas and virtual lists, plus a neutral `NodeScope` for logical workspace
  panels. Audio seek/waveform controls report units, values, ranges and actions.
- Added `ui.sessions` and deferred `ui.snapshot` commands to Audio Player. Snapshot
  documents are serialized on the game lane, returned as nested JSON, and paged
  through bounded retained snapshots and cursors.
- Added `view: "overview"` while preserving `view: "full"` as the default.
  Overview organizes visible and inactive tabs by the actual Workspace split
  tree, summarizes Now Playing and Spectrum, and associates Playlist cells with
  column names; full snapshots retain bounded chart samples.
- Structured command results now validate an object payload before embedding it
  under `data`; cancellation liveness, a five-second capture timeout and host
  shutdown complete/cancel waiters.

## What changed
- Architecture or behavior: UI/render lane publishes immutable copies; Audio
  Player owns session command registration and snapshot retention; Runtime command
  owns generic structured JSON response handling.
- Important files/modules: Editor UI API/host, Editor Workspace, Audio Player
  editor/host, Runtime command endpoint/registry.
- Public API or ownership changes: added `UiSnapshotMailbox`, workspace snapshot
  projection, structured JSON result field, and request-liveness callback. Audio
  Player mode adds dependencies on neutral UI widgets and nlohmann JSON; Runtime
  command has no Editor dependency.

## Validation
- Required level: L3
- Command: `cmake --build build --config Debug --target AudioPlayerMode -- /m:1`
- Result: PASS
- Evidence: Audio Player module and affected Editor/Runtime dependencies built.
- Command: `cmake --build build --config Debug --target KimPeanutEngine -- /m:1`
- Result: PASS
- Evidence: linked `build/Debug/KimPeanutEngine.exe`.
- Command: `tools/agent/placement.ps1` for Editor UI, Workspace, Audio host and
  Runtime command paths with `UiSnapshot|UiFrameResult|RegisterHostCommands|
  CommandCompletionSink|EditorWorkspace::Snapshot|request_is_active`.
- Result: PASS
- Evidence: structural placement checks passed; semantic ownership review completed.
- Command: visible Debug Vulkan Audio Player on agent port 37373; execute
  `ui.sessions`, execute `ui.snapshot`, then poll the returned request ID.
- Result: PASS
- Evidence: one Audio Player session; snapshot request completed successfully with
  nested object `data.snapshot`, frame `1`, 125 nodes and one declared style. All
  seven workspace panels linked to nodes, six active panels had child nodes, and
  node IDs were unique. Earlier scalar checks found no array-valued parent IDs or
  one-element null geometry fields. Coverage correctly reports partial and 40
  nodes without measured geometry.
- Command: `git diff --check`
- Result: PASS

## Remaining risks and unverified areas
- The ImGui adapter does not provide clipped bounds, occlusion/stacking or resolved
  style values; snapshots enumerate these as unavailable and report partial coverage.
- Headless session discovery, multiple attached sessions, paging with a large
  playlist, expired cursors, cancellation, failed submission and shutdown during
  capture were not exercised against a running process.
- Recording-backend parity and ordinary-frame cost were reviewed in code but not
  exercised by tests.
- CTest was not run; this scoped task did not call for a broad test run.

## Remaining work
- Exercise the listed limits and lifecycle edge cases, including a response that
  spans multiple pages and cancellation while capture is in flight.
- Add resolved/clipped geometry and occlusion fields when the neutral backend can
  supply them without fabricating measurements.
- Attach this provider to additional GUI hosts only after they expose the same
  neutral UI/workspace seam.

## Documentation and follow-up
- Updated the Editor roadmap to record the Audio Player pilot and remaining
  acceptance work.
- The design proposal remains at `docs/editor/.plan/ui-session-snapshot.md`.

## Overview follow-up — 2026-11-11

- Operation and acceptance: provide a small `view: "overview"` result that
  explains the live Audio Player layout and playback state, while `full` keeps
  the existing detailed JSON and bounded chart samples.
- Existing candidates inspected: `UiSnapshotCapture` and `EditorWorkspace::Snapshot`
  already own immutable frame observations and actual split/tab placement;
  Audio's stable IDs, playback controls and spectrum plot already report the
  relevant domain values. Reused these instead of adding a second capture path.
- Placement and reason: `AudioPlayerHost::OverviewJson` projects Audio-specific
  meaning from the captured neutral snapshot; plot samples/axis observations
  remain owned by Editor UI API. Audio volume now explicitly publishes its
  percent value/range for the generic observer.
- Validation: `cmake --build build --config Debug --target KimPeanutEngine -- /m:1`
  passed. Queried `ui.snapshot` with `view: "overview"` through the visible
  Vulkan Audio Player on port 37373; it returned nested JSON with split tree,
  visible Library/Playlists/Now Playing/Queue/Info/Spectrum panels, Output Log
  marked `inactive_tab`, queue filter/columns/count, playback values and the
  no-source spectrum axes/state. `tools/agent/placement.ps1` and
  `git diff --check` passed. No CTest run.
- Limitations: the live player had no track selected, so populated playback,
  subtitle and numeric RMS/peak values were not exercised; their empty/idle
  semantics were returned correctly. Full-view chart samples were not exercised
  against an active source. Backend clipping, occlusion and resolved style remain
  unavailable as reported by partial coverage.

## GUI Semantic Interface — 2026-11-11

- Operation and acceptance: expose the current completed Editor UI frame through
  generic tree, element-detail and element-style queries; callers must not need
  Audio Player or other feature schema to inspect a GUI.
- Existing candidates inspected: `UiContext::EndFrame()` already merges semantic
  and backend observations; `UiSession` owns the backend/context lifetime;
  `EditorUiHost` held a duplicate last-frame result. Kept frame ownership in
  `UiSession` and made the host read the retained frame for its immutable mailbox
  capture.
- Placement and reason: `UiSemanticInterface` sits in the Editor UI API and is
  owned by `UiSession`, because the session owns frame lifetime and UI IDs. It
  exposes `GetUITree()`, `GetElementInfo(id)` and `GetElementStyle(id)` as
  read-only view queries. Declared `UiTheme` values are returned separately
  from backend-resolved style data, which is currently unavailable.
- Dependencies and lifetime: no new Runtime dependency; tree/details/style
  results are session-owned read-only views, avoiding copies of chart data.
  Views expire when the next frame is completed. Incomplete frames are marked
  and element/style lookups return no result.
- Validation at this implementation checkpoint: Debug `KimPeanutEngine` build
  passed after fixing in-progress Audio overview calls to use the renamed
  `PanelElements` helper. Focused semantic-interface tests were added in the
  follow-up below; no live command call was performed.
- Remaining boundary: the current command serialization/registration still
  resides in `AudioPlayerHost`; moving remote query transport to application
  composition and proving it works with a non-Audio session remain open.
- Structured unknown-GUI check: `GetUITree()` returned one Session root, a
  `Control Surface` window, and a selected `Playback` panel with three child
  controls. `GetElementInfo(play)` reported a Button with `activate`;
  `GetElementInfo(volume)` reported Slider, 72 `%`, range 0–100 and fixture
  bounds `[24, 88]–[456, 112]`; `GetElementInfo(mute)` reported unchecked
  Checkbox with `toggle`. `GetElementStyle(volume)` returned the declared
  accent color `{0.10, 0.80, 0.95, 1.0}` and explicitly marked resolved style
  unavailable. From these records alone, the interface's basic purpose and
  control state were identifiable. The test exposed and fixed a window-parent
  merge bug that had incorrectly surfaced the window as a second tree root.
- Validation: targeted `ctest --test-dir build -C Debug -R
  '^UiRecordingContractTest\.' --output-on-failure` passed **17/17**; full
  `cmake --build build --config Debug --target KimPeanutEngine -- /m:1` passed.

## Geometry, consistency and role semantics — 2026-11-11

- Operation: replace fixture-only expectations with actual ImGui item and
  window bounds, active clip intersections and per-frame visibility; pin the
  three semantic queries to one immutable completed frame; define optional
  value and state fields by role.
- Placement: the ImGui adapter owns backend-measured geometry and interaction
  facts. `UiSemanticInterface` owns immutable query snapshots and derives only
  logical container extents from observed children, marking those extents as
  `derived_from_children`. `EditorUiHost` copies those derived measurements
  into its value-only capture. Audio serializes coordinate and semantic
  contracts alongside the captured hierarchy.
- Coordinate contract: rectangles use ImGui main-viewport logical coordinates;
  viewport-local coordinates subtract the viewport origin, and framebuffer
  coordinates multiply by framebuffer scale. Clipped bounds intersect the
  item, active ImGui clip and viewport work area. Visibility does not claim
  freedom from higher-window occlusion.
- Semantic contract: intrinsic `value` is absent for roles such as Button;
  checkbox state remains `value`; `selected` is role-specific; active, focused,
  hovered and visible states are optional so unavailable is not confused with
  false. A retained `UiSemanticView` keeps tree, element and style queries on
  its original completed frame.
- Validation: Debug `UiRecordingContractTest` built and passed 17/17 tests;
  `KimPeanutEngine` Debug rebuilt successfully. A visible Debug Vulkan Audio
  Player returned a full `ui.snapshot` frame `7034` (`snapshot_id` `1/7034`):
  141 nodes, one declared style, one workspace and seven registered panels.
  The Library table contained six project tracks; the playback queue was empty.
  140 nodes have measured or derived geometry; the inactive Output Log panel
  is the single node without bounds. No snapshot records were omitted.
- Artifact: `save/validation/audio-player-gui-snapshot.json` is the complete
  full-view response, including layout, parent IDs, controls, values/actions,
  geometry, declared theme and coverage. The file is ignored under `save/`.
- Remaining limits: higher-window occlusion and backend-resolved style values
  are not available. The Audio-specific command provider/serializer, paging,
  cancellation and failure-path cases remain outside this acceptance pass.

## Query projections and incremental changes — 2026-11-12

- Operation and acceptance: extend the existing `UiSemanticInterface` with
  typed Semantic/Spatial/Visual projections and bounded frame deltas; make
  Audio Player's default response semantic and avoid emitting both its generic
  flat node list and its panel controls.
- Existing candidates inspected: `UiSession` already owns the immutable
  `UiSemanticView`; `UiSemanticSnapshot` already builds ElementID, SemanticID
  and StyleID indexes; `UiSnapshotCapture` is the published immutable frame
  used by the deferred command path. Extended these owners rather than adding
  another registry or capture path.
- Placement and ownership: `UiSemanticInterface` now owns projection and
  32-entry change history; `UiSession` exposes the version query. The Audio
  serializer chooses a response projection on the game lane. Non-full views
  build one panel/control JSON representation; only legacy `full` constructs
  the duplicate flat `nodes` section. Semantic mode strips geometry, Spatial
  adds shared layout/coordinate data, Visual adds style references and the
  deduplicated theme table.
- Semantics: ElementID remains a numeric C++ identity and is serialized as a
  JSON string; SemanticID remains the optional stable business key. Semantic
  query results are owned copies that omit geometry; spatial/visual results add
  geometry, and only visual adds style IDs/definitions. Null or false states
  are preserved when role-meaningful; irrelevant interaction states are
  omitted by role. Plot samples are excluded from Semantic/Spatial queries but
  axes and sample counts remain available.
- Incremental behavior: only completed observed frames publish. A completed
  observed empty frame is retained and reports removals; unobserved frames do
  not advance history. Changes distinguish additions/removals/updates and
  changed field groups. The ring stores changed IDs, SemanticIDs, masks and
  bounded semantic projections for added/updated records; chart samples and
  geometry are not duplicated in those records. It does not retain another
  full frame. Old session, expired
  version, or broken history chain returns `FullSnapshotRequired`.
- Validation: Debug targets `UiRecordingContractTest` and `AudioPlayerMode`
  built. CTest was previewed with `ctest --test-dir build -C Debug -N -R
  '^UiRecordingContractTest\.'` (18 tests), then all 18 passed. The new test
  covers query levels, ID/role filtering, value/style changes, additions,
  removals and session mismatch. Placement checks passed and `git diff
  --check` reported no whitespace errors.
- Measurement evidence: the existing full artifact has 239,767 raw bytes and
  the user-supplied compact reference has 18,276 raw bytes; both carry snapshot
  ID `1/7034`, so that reference is 92.38% smaller by raw bytes. This is a
  comparison of two saved artifacts, not output from the modified serializer.
  An offline structural projection prototype from the full artifact produced
  about 22.9 KB before the final role-state pruning and metadata additions.
  Exact current serializer bytes, token counts, query/serialization latency,
  and incremental memory have not yet been measured. Python has no `tiktoken`
  install or cached target tokenizer; the task did not name a tokenizer, and
  network installation was not attempted.
- Remaining verification: capture the modified `ui.snapshot` response from a
  running Audio Player; count tokens with the selected target tokenizer; time
  query and serialization separately; measure delta-ring memory on that frame.
  Higher-window occlusion and resolved ImGui styles remain unavailable in the
  existing backend contract.

## Visual acceptance and snapshot failure — 2026-11-12

- Visual evidence: Audio Player was visible at 1280x720 with Library, Now
  Playing, Info, Playlists, Queue and Spectrum panels. The layout was readable
  with no obvious panel overlap. Library and queue were empty, so populated
  tracks and playback were not visually checked.
- Reproduction: `ui.sessions` succeeded. Semantic `ui.snapshot` returned a
  serialization failure with `invalid unordered_map<K, T> key`; `view: "full"`
  succeeded with 141 nodes and one workspace. The full capture showed every
  region's `active_tab` was already the string form of its panel `node_id`.
- Root cause: `OverviewJson` treated `active_tab` as a workspace panel ID and
  looked it up in a map keyed by values such as `audio_library`. The key was
  actually a stable numeric node ID string, so `unordered_map::at` threw.
  Without the exception boundary, this failure would escape the host tick;
  with the catch, the command returned failure and the engine remained
  available. A sandbox process query did not see the externally launched app;
  an external process query confirmed it was still running, correcting the earlier
  observation that the app had exited.
- Fix: the overview now uses the stable `active_tab` node ID directly as
  `active_panel`, matching the IDs already emitted in each region's `panels`.
  Snapshot failures now report their stage and original exception in the
  command result and log.
- Validation: Debug `KimPeanutEngine` build passed. A new live semantic capture
  succeeded (`snapshot_id` `1/1506`) with seven panels and six regions. Every
  active panel reference resolves to an emitted panel ID, and no semantic
  control contains spatial fields. The app remained running after capture.
  The live full capture and semantic result are diagnostic files under
  `save/validation/`. `git diff --check` and placement checks passed. The
  targeted 18 `UiRecordingContractTest` cases passed earlier in this task;
  they were not rerun for this Audio-only projection fix.
# Background query correction — 2026-11-12

- Operation and acceptance: query the most recently submitted GUI frame from a
  background command call without waiting for the next UI frame; a snapshot ID
  identifies the immutable frame returned.
- Existing candidates inspected: `UiSession::Query()` retains a thread-safe
  semantic view; `EditorUiHost::RenderFrame()` owns ImGui-frame observation,
  workspace sampling and successful-submit timing; `UiSnapshotMailbox` coupled
  capture to a next-frame request; `CommandRegistry` invokes Immediate commands
  on their caller thread.
- Placement and reason: replaced the request mailbox with `UiSnapshotStore` in
  Editor UI API because it owns immutable capture publication/lifetime. The UI
  host gathers ImGui and workspace data on the UI thread; Audio Player performs
  projection, JSON serialization and paging on the command caller thread.
- Consumers, ownership/lifetime, and validation: published captures are
  `shared_ptr<const ...>` values protected by a store mutex and cleared by
  session ID on host close. Audio paging/cursor caches use a separate mutex.
  Pending callbacks and frame waits are removed. `AudioPlayerMode` compiled.
  The final `KimPeanutEngine` link was blocked because the running executable
  holds `build/Debug/KimPeanutEngine.exe` open (LNK1168); the existing window
  was left untouched. `git diff --check` and the final placement review passed.
  Runtime verification against the changed executable remains unverified; no
  application restart is planned for this correction.

# Editor-owned session registration — 2026-11-12

- Operation and acceptance: make the Editor object that owns a UI session
  register and release `ui.sessions` / `ui.snapshot`; application Hosts must
  not own those query commands or snapshot cache state.
- Existing candidates inspected: `Editor::InitEditorUI()` owns the standard
  Editor lifetime; `AudioPlayerEditor::Initialize/Shutdown()` owns standalone
  Audio UI lifetime; `AudioPlayerHost::RegisterHostCommands()` owns audio
  domain commands; `EditorUiHost` publishes immutable completed-frame captures.
- Placement and reason: `Editor` and `AudioPlayerEditor` now hold their
  session stores and command-registration tokens. The generic serializer and
  registration helper live in `EditorUISessionLib`, which has no ImGui
  dependency. This lets distinct Editor clients register at their own
  lifecycle boundary while sharing the protocol implementation.
- Consumers, ownership/lifetime, and validation: immediate query callbacks
  read only `UiSnapshotStore` and serialize the last submitted immutable
  capture. The Audio Editor unregisters commands before closing the UI and
  clearing its session. Debug `KimPeanutEngine` build passed. In the final
  regular `scene3d` run, `ui.sessions` returned session `1`; after the loading
  frame, the visual snapshot returned frame `13448`, 181 elements, 3 declared
  styles, and 2 elements without measured geometry. Output is saved under
  `save/validation/scene3d-ui-visual-structured-20261112.json`.
