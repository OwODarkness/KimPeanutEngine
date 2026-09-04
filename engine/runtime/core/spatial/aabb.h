#ifndef KPENGINE_RUNTIME_CORE_SPATIAL_AABB_H
#define KPENGINE_RUNTIME_CORE_SPATIAL_AABB_H

#include <algorithm>
#include <array>
#include <limits>

#include "math/math_header.h"

namespace kpengine::spatial
{
    // Canonical axis-aligned bounds shared by World, Physics, Render, and tools.
    // Spatial values may use math primitives, but math has no dependency on spatial.
    struct AABB
    {
        Vector3f min_{};
        Vector3f max_{};

        bool IsValid() const
        {
            return min_.x_ <= max_.x_ && min_.y_ <= max_.y_ && min_.z_ <= max_.z_;
        }

        std::array<Vector3f, 8> GetCorners() const
        {
            return {{
                {min_.x_, min_.y_, min_.z_},
                {min_.x_, min_.y_, max_.z_},
                {min_.x_, max_.y_, min_.z_},
                {min_.x_, max_.y_, max_.z_},
                {max_.x_, min_.y_, min_.z_},
                {max_.x_, min_.y_, max_.z_},
                {max_.x_, max_.y_, min_.z_},
                {max_.x_, max_.y_, max_.z_},
            }};
        }

        void ExpandToInclude(const Vector3f &point)
        {
            min_.x_ = std::min(min_.x_, point.x_);
            min_.y_ = std::min(min_.y_, point.y_);
            min_.z_ = std::min(min_.z_, point.z_);
            max_.x_ = std::max(max_.x_, point.x_);
            max_.y_ = std::max(max_.y_, point.y_);
            max_.z_ = std::max(max_.z_, point.z_);
        }

        bool operator==(const AABB &rhs) const
        {
            return min_ == rhs.min_ && max_ == rhs.max_;
        }

        bool operator!=(const AABB &rhs) const
        {
            return !(*this == rhs);
        }
    };

    inline AABB TransformAABB(const AABB &local_bounds, const Transform3f &transform)
    {
        if (!local_bounds.IsValid())
        {
            return local_bounds;
        }

        const float maximum = std::numeric_limits<float>::max();
        AABB world_bounds{{maximum, maximum, maximum},
                          {-maximum, -maximum, -maximum}};
        for (const Vector3f &corner : local_bounds.GetCorners())
        {
            const Vector3f transformed =
                transform.rotator_.RotateVector(transform.scale_ * corner) + transform.position_;
            world_bounds.ExpandToInclude(transformed);
        }
        return world_bounds;
    }
}

#endif
