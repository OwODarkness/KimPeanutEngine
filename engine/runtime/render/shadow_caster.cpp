#include "shadow_caster.h"

namespace kpengine
{

    void ShadowCaster::SetShadowMapSize(int width, int height)
    {
        width_ = width;
        height_ = height;
    }
    void ShadowCaster::SetNearPlane(float near)
    {
        near_plane_ = near;
    }
    void ShadowCaster::SetFarPlane(float far)
    {
        far_plane_ = far;
    }

}