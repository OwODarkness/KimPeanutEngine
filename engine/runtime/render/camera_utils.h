#ifndef KPENGINE_RUNTIME_RENDER_CAMERA_UTILS_H
#define KPENGINE_RUNTIME_RENDER_CAMERA_UTILS_H

#include "math/math_header.h"
#include "spatial/aabb.h"

namespace kpengine::render::camera
{
    // Tests whether any AABB corner lies inside one perspective camera face.
    // The view matrix uses the CPU-side math convention and looks down -Z.
    // Invalid bounds remain visible as the safe producer-data fallback.
    bool IsAABBInsidePerspectiveFace(const spatial::AABB &bounds,
                                     const Matrix4f &view, float near_plane,
                                     float far_plane);
}

#endif
