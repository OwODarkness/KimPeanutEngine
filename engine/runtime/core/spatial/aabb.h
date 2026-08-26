#ifndef KPENGINE_RUNTIME_CORE_SPATIAL_AABB_H
#define KPENGINE_RUNTIME_CORE_SPATIAL_AABB_H

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

        bool operator==(const AABB &rhs) const
        {
            return min_ == rhs.min_ && max_ == rhs.max_;
        }

        bool operator!=(const AABB &rhs) const
        {
            return !(*this == rhs);
        }
    };
}

#endif
