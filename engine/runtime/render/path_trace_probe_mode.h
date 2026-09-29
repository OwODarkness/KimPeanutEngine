#ifndef KPENGINE_RUNTIME_RENDER_PATH_TRACE_PROBE_MODE_H
#define KPENGINE_RUNTIME_RENDER_PATH_TRACE_PROBE_MODE_H

#include <cstdint>

namespace kpengine::render
{
    enum class PathTraceProbeMode : uint32_t
    {
        Beauty = 0,
        PrimaryVisibility = 1,
        PrimaryNormal = 2,
        PrimaryAlbedo = 3,
        DirectOnly = 4,
        SurfaceParameters = 5,
        RayQueryVisibility = 6,
        RayConeFiltering = 7,
        LowSppPreview = 8,
        BeautyDenoise = 9,
        SingleLightPreview = 10,
    };
}

#endif
