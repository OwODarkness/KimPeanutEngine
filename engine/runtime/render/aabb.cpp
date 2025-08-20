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

        bool AABB::Intersect(const Vector3f& origin, const Vector3f& dir, float out_t) const
        {

            float tmin = (min_.x_ - origin.x_) / dir.x_;
            float tmax = (max_.x_ - origin.x_) / dir.x_;

            if (tmin > tmax)
                std::swap(tmin, tmax);

            float tymin = (min_.y_ - origin.y_) / dir.y_;
            float tymax = (max_.y_ - origin.y_) / dir.y_;

            if (tymin > tymax)
                std::swap(tymin, tymax);

            if ((tmin > tymax) || (tymin > tmax))
                return false;

            if (tymin > tmin)
                tmin = tymin;
            if (tymax < tmax)
                tmax = tymax;

            float tzmin = (min_.z_ - origin.z_) / dir.z_;
            float tzmax = (max_.z_ - origin.z_) / dir.z_;

            if (tzmin > tzmax)
                std::swap(tzmin, tzmax);

            if ((tmin > tzmax) || (tzmin > tmax))
                return false;

            if (tzmin > tmin)
                tmin = tzmin;
            if (tzmax < tmax)
                tmax = tzmax;

            if (tmin < 0.0f && tmax < 0.0f)
                return false;

            out_t = tmin >= 0.0f ? tmin : tmax;
            return true;
        }

}