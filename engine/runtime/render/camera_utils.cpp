#include "render/camera_utils.h"

#include <cmath>

namespace kpengine::render::camera
{
    bool IsAABBInsidePerspectiveFace(const spatial::AABB &bounds,
                                     const Matrix4f &view, float near_plane,
                                     float far_plane)
    {
        if (!bounds.IsValid())
        {
            return true;
        }

        for (const Vector3f &corner : bounds.GetCorners())
        {
            const Vector4f view_space = view * Vector4f{corner, 1.0f};
            const float depth = -view_space.z_;
            if (depth >= near_plane && depth <= far_plane &&
                std::abs(view_space.x_) <= depth &&
                std::abs(view_space.y_) <= depth)
            {
                return true;
            }
        }
        return false;
    }
}
