#include "aabb.h"

namespace kpengine{
    AABB GetWorldAABB(const AABB &aabb, const Matrix4f &transform)
    {
        AABB res;
        Vector3f min_ = aabb.min_;
        Vector3f max_ = aabb.max_;
        Vector3f corner[8] = {
            {min_.x_, min_.y_, min_.z_},
            {max_.x_, min_.y_, min_.z_},
            {max_.x_, max_.y_, min_.z_},
            {min_.x_, max_.y_, min_.z_},

            {min_.x_, min_.y_, max_.z_},
            {max_.x_, min_.y_, max_.z_},
            {max_.x_, max_.y_, max_.z_},
            {min_.x_, max_.y_, max_.z_}};

        for (int i = 0; i < 8; i++)
        {
            Vector3f point = Vector3f(transform * Vector4f(corner[i], 1.f));
            res.Expand(point);
        }
        return res;
    }
}