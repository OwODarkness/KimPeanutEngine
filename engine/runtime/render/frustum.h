#ifndef KPENGINE_RUNTIME_FRUSTUM_H
#define KPENGINE_RUNTIME_FRUSTUM_H

#include "runtime/core/math/math_header.h"
#include "aabb.h"

namespace kpengine{
    struct Frustum{
        Vector4f planes[6];

        bool Contains(const AABB& world_aabb) const
        {
            for(int i = 0;i<6;i++)
            {
                const Vector4f& plane = planes[i];

                Vector3f positive = world_aabb.min_;
                if(plane.x_ >=0) positive.x_ = world_aabb.max_.x_;
                if(plane.y_ >=0) positive.y_ = world_aabb.max_.y_; 
                if(plane.z_ >=0) positive.z_ = world_aabb.max_.z_; 

                if(Vector3f(plane).DotProduct(positive) + plane.w_ < 0)
                {
                    return false;
                }
            }
            return true;
        }
    };

    Frustum ExtractFrustumFromVPMat(const Matrix4f& vp_mat);
}

#endif