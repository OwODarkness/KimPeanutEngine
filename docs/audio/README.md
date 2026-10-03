# Standalone Audio Player

Run these commands from the repository root in PowerShell. `RelWithDebInfo` is
the recommended non-Debug configuration. For a fully optimized Release build,
replace `RelWithDebInfo` with `Release` in the build configuration and exe path.

```powershell
cmake --build build --config RelWithDebInfo --target KimPeanutEngine
Start-Process -FilePath .\build\RelWithDebInfo\KimPeanutEngine.exe `
  -ArgumentList @('--mode', 'audio-player', '--graphics-api', 'vulkan')
```

The Audio Player opens as a GUI window. In the Library dock, choose **ADD FILES**
or enter a path and press **QUEUE FILE**. Select a track in the playlist, then
press **Play** in Now Playing. Subtitle selection is optional. See
[Audio architecture](PLANS.md) and the [Audio roadmap](TODO.md) for more detail.
