# Standalone TTS editor architecture

Parent: [TTS design map](../PLANS.md). Stages:
[TE1](.plan/TE1.md) and [TE2](.plan/TE2.md). Acceptance:
[TODO.md](TODO.md). Visual reference:
[`save/tts_gui.png`](../../../save/tts_gui.png) (local draft, ignored by Git).

## Boundary and data flow

The editor is a standalone `--mode tts` application host, following the
existing [Audio Player host](../../../engine/runtime/audio/editor/audio_player_host.cpp)
and registration pattern. Its source lives in `engine/module/tts/editor/` and
builds as a separate Editor-facing target; the existing `TTS` target stays
headless and has no ImGui dependency. `ApplicationMode`, parser/name/index,
launch tests, and module registration gain one mode.

```text
config/tts/settings.json -> TtsEditorSettings -> TtsEditorController -> TTSRequest
                                                 |               |
ImGui snapshot <- host Tick/event drain <--------+        TTSSystem worker
    |                                                         |
actions -> controller                                 Audio player handle
                                                         |
                                                  Runtime Audio / Speech bus
```

The host initializes the controller and local settings, then the render-thread
editor. Generation initializes the Audio device before the TTS provider. The
host shuts down the editor,
cancels and joins TTS jobs, retires remaining Audio handles, and finally shuts
down Audio. The controller must not let worker callbacks mutate ImGui state:
copy results into its own thread-safe inbox and consume them on the host's
update thread. `DrainEvents()` supplies job states; `Completed` means audible
drain, while network completion is separate.

## Settings and voice identity

The source of truth is `project_root/config/tts/settings.json`, resolved from
`project_root` rather than the process working directory. If missing, create
its parent and a schema-versioned template atomically, then load it. The
template uses empty `address`, `ref_audio_path`, and `ref_text`, and port `0`;
the user must supply real values before Generate becomes available. These
four fields must never be compiled into the GUI, a demo preset, or a checked-in
JSON file. Existing files at `project_root/tts/settings.json` are copied to
the new path only when it does not exist, preserving malformed input for repair.
Both paths are ignored by Git. A minimal planned schema is:

```json
{
  "schema_version": 1,
  "server": { "address": "", "port": 0, "api_path": "/tts", "timeout_seconds": 180 },
  "voices": [
    { "id": "default", "name": "Default", "ref_audio_path": "", "ref_text": "", "ref_language": "" }
  ],
  "selected_voice_id": "default",
  "text_language": "",
  "playback_mode": "stream",
  "output_directory": "tts/output"
}
```

`address` maps to `ServerConfig::host`; port and API path map to the same
structure. A selected voice maps server-side `ref_audio_path`, `ref_text` to
`TTSRequest::ref_audio_path`, `prompt_text`, and `ref_language` to
`prompt_lang`; editor text and target language map to `text`, `text_lang`.
`playback_mode` accepts `stream` or `buffer` and maps to
`TTSRequest::streaming`. The selected mode is copied into each dialog entry
when Generate is pressed. Changing the setting affects new jobs only; an
active result keeps the controls and metadata of its actual player type.
Validate the address, port range, prompt/reference pair, and language before
submitting. Editing server connection values requires a controlled provider
reinitialize after active work is cancelled; editing a voice affects only new
requests. Failed parsing preserves the invalid file for repair and reports the
error in the UI. Saves use a temporary file and atomic replacement.
Do not print reference text or absolute reference paths to routine logs.

## Draft-to-function mapping

| Draft region | TE1 behavior | Existing base / missing work |
|---|---|---|
| Audio Preview | Switch between stream and buffer control panels from the settings-backed playback mode, then show state, elapsed played time, pause/resume, stop/cancel, and Speech volume | `AudioSystem::GetAudioPlayer`, `AudioPlayer` state/cursor/volume, `TTSSystem::DrainEvents`; host-owned snapshot adapter. Stream cannot seek or change rate; buffer can seek and set preview playback rate. |
| Dialog List | Compact searchable table with stable IDs, text, voice, WAV duration, job state, inline play/cancel, and playlist-style orange selection | The TE2 library persists completed rows and supports stable-ID import, duplicate, delete, and restore. The table clips off-screen rows and reads canonical WAV duration metadata without loading each preview. |
| TTS Control | Voice preset, target language, connection/settings access | Settings-backed voice model is new; current `TTSRequest` has no Emotion or Pitch fields. |
| Text Input | Multiline text, length limit, Generate, busy/error state | `TTSSystem::AsyncSynthesize` and `Cancel`; no UI-thread synchronous call. |
| Output | Destination and filename for canonical WAV export | Core TTS returns bounded immutable provider WAV bytes; the editor canonicalizes and owns artifacts and exports them atomically. |

The bundled portrait is display artwork. Duration, sample format, and waveform
are derived from the canonical WAV artifact; no waveform is fabricated while a
stream has not returned audio bytes. Synthesis speed, pitch, emotion, date, and
file controls remain placeholders until backed by real data and behavior. A
stream's elapsed time comes from the
played-frame cursor; its total duration is unknown until trustworthy metadata
exists. The stream panel exposes state, pause/resume, stop/cancel, and volume;
its seek and rate controls are absent or disabled. A buffered clip may expose
known duration, seek, and preview playback rate through `BufferAudioPlayer`.
This rate does not change synthesis speed or pitch. `TTSResult` returns the
handle after synthesis finishes, so controls during progressive network
delivery need a safe active-job snapshot/handle seam; TE1 may show generation
state first and enable transport after handoff. Reuse
`EditorUILib::DrawTransportStrip` for transport
semantics and action separation; only reuse Audio Player's preview/widget when
its data contract fits, without making TTS depend on `AudioPlayerMode`.

The sketch's major panes remain recognizable, but Editor chrome and docking
follow the existing mode shell. At narrow widths stack or tab the right panes;
empty, invalid settings, disconnected server, queued, generating, buffering,
playing, paused, completed, cancelled, and failed states remain visible. No
fictional waveform or output metadata should be drawn.
