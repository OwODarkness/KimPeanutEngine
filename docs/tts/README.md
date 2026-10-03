# Standalone TTS Editor

Run these commands from the repository root in PowerShell. `RelWithDebInfo` is
the recommended non-Debug configuration. For a fully optimized Release build,
replace `RelWithDebInfo` with `Release` in the build configuration and exe path.

```powershell
cmake --build build --config RelWithDebInfo --target KimPeanutEngine
Start-Process -FilePath .\build\RelWithDebInfo\KimPeanutEngine.exe `
  -ArgumentList @('--mode', 'tts', '--graphics-api', 'vulkan')
```

On first launch, the editor creates `config/tts/settings.json`. Set the TTS
server address and port, reference audio path and reference text there, or edit
them in **TTS Control** and press **Save Settings**. Reference audio paths must
be visible to the TTS server. Existing `tts/settings.json` settings are copied
to the new location automatically when needed. See the [TTS design map](PLANS.md)
and [editor architecture](editor/PLANS.md) for more detail.
