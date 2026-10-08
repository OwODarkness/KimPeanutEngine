#ifndef KPENGINE_RUNTIME_RENDER_TEMPORAL_REPROJECTION_H
#define KPENGINE_RUNTIME_RENDER_TEMPORAL_REPROJECTION_H

#include "render/render_camera.h"

namespace kpengine::render
{
    // Motion is previous-minus-current normalized render-target UV. UV uses a
    // top-left origin on both APIs; projection jitter is removed exactly once.
    struct RasterMotionSample
    {
        Vector2f motion_uv{};
        float view_depth = 0.0f;
        bool history_valid = false;
    };

    RasterMotionSample ComputeRasterMotionSample(
        const Vector4f &current_clip, const Vector4f &previous_clip,
        const Vector2f &current_jitter_uv, const Vector2f &previous_jitter_uv,
        float current_view_depth, bool previous_surface_valid,
        bool vulkan_clip_depth) noexcept;

    bool IsRasterCameraCut(const CameraData &previous,
                           const CameraData &current) noexcept;
}

#endif
