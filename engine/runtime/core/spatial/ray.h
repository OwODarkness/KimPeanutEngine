#ifndef KPENGINE_RUNTIME_CORE_SPATIAL_RAY_H
#define KPENGINE_RUNTIME_CORE_SPATIAL_RAY_H

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <optional>

#include "spatial/aabb.h"

namespace kpengine::spatial
{
    struct Ray
    {
        Vector3f origin{};
        Vector3f direction{0.0f, 0.0f, -1.0f};

        bool IsValid() const noexcept
        {
            return std::isfinite(origin.x_) && std::isfinite(origin.y_) &&
                   std::isfinite(origin.z_) && std::isfinite(direction.x_) &&
                   std::isfinite(direction.y_) && std::isfinite(direction.z_) &&
                   direction.SquareLength() > 1.0e-12f;
        }
    };

    // Returns the nearest non-negative ray parameter. Callers should provide a
    // normalized direction when they want the result to represent world units.
    inline std::optional<float> IntersectRayAABB(const Ray &ray,
                                                 const AABB &bounds) noexcept
    {
        if (!ray.IsValid() || !bounds.IsValid())
        {
            return std::nullopt;
        }

        constexpr float kParallelEpsilon = 1.0e-7f;
        float near_distance = 0.0f;
        float far_distance = std::numeric_limits<float>::infinity();

        for (std::size_t axis = 0; axis < 3; ++axis)
        {
            const float origin = ray.origin[axis];
            const float direction = ray.direction[axis];
            if (std::abs(direction) <= kParallelEpsilon)
            {
                if (origin < bounds.min_[axis] || origin > bounds.max_[axis])
                {
                    return std::nullopt;
                }
                continue;
            }

            float axis_near = (bounds.min_[axis] - origin) / direction;
            float axis_far = (bounds.max_[axis] - origin) / direction;
            if (axis_near > axis_far)
            {
                std::swap(axis_near, axis_far);
            }
            near_distance = std::max(near_distance, axis_near);
            far_distance = std::min(far_distance, axis_far);
            if (near_distance > far_distance)
            {
                return std::nullopt;
            }
        }

        return near_distance <= far_distance ? std::optional<float>{near_distance}
                                              : std::nullopt;
    }
}

#endif
