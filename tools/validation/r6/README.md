# R6 Runtime replay

`runtime_replay.py` drives a checked-in Sponza camera fixture through the Runtime
command registry and saves profile evidence under `save/diagnostics/`. It expects
the Runtime to be launched visibly on the Default desktop with Vulkan, the
`level/sponza.level` startup fixture, and agent port `37373`.

```powershell
python tools/validation/r6/runtime_replay.py --preset adaptive --motion --settle-frames 30 --capture `
  --output save/diagnostics/r6-motion-adaptive.json
```

The replay verifies the active path-tracing settings, viewport, full tracked
texture residency, and authored light count. It records the effective adaptive
sampling state and sample count after each camera keyframe, followed by a
120-warmup/300-sample Runtime profile. Keyframes are discrete pose changes; this
is useful for checking threshold transitions but is not a continuous-input
camera-motion benchmark.

Presets include fixed 1 SPP raw/guided, one-light guided, 2 SPP raw, 4 SPP raw,
and adaptive camera motion. Adaptive camera motion is the default Runtime
sampling policy; fixed sampling remains explicitly selectable. Adaptive uses
configurable 1-SPP reconstruction while moving (default `guided_preview`,
the stronger 3x3 normal/depth-guided spatial filter),
then accumulates in raw mode: default 4 SPP below 100 samples, 2 SPP from 100
through 199, and 1 SPP from 200 onward. Both thresholds and each SPP rate are
configurable. The accumulated raw history remains intact at both SPP
transitions. The moving filter is spatial; temporal reprojection remains a
separate R6 stage.
