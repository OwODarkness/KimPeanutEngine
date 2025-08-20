#ifndef KPENGINE_RUNTIME_AABB_H
#define KPENGINE_RUNTIME_AABB_H

#include "runtime/core/math/math_header.h"
#include <limits>

namespace kpengine
{
    struct AABB
    {
        Vector3f min_;
        Vector3f max_;
        AABB()
        {
            float max_value = std::numeric_limits<float>::lowest();
            float min_value = std::numeric_limits<float>::max();

            min_ = Vector3f(min_value);
            max_ = Vector3f(max_value);
        }
        AABB(const Vector3f &min_point, const Vector3f &max_point) : min_(min_point), max_(max_point) {}

        void Expand(const Vector3f &point)
        {
            if (point.x_ < min_.x_)
                min_.x_ = point.x_;
            if (point.y_ < min_.y_)
                min_.y_ = point.y_;
            if (point.z_ < min_.z_)
                min_.z_ = point.z_;

            if (point.x_ > max_.x_)
                max_.x_ = point.x_;
            if (point.y_ > max_.y_)
                max_.y_ = point.y_;
            if (point.z_ > max_.z_)
                max_.z_ = point.z_;
        }

        bool Intersect(const Vector3f& origin, const Vector3f& dir, float out_t) const;

        Vector3f Center() const { return 0.5f * (min_ + max_); }
        Vector3f Extent() const { return 0.5f * (max_ - min_); }
    };

    AABB GetWorldAABB(const AABB &aabb, const Matrix4f &transform);
}

#endif