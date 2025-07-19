#include "primitive_scene_proxy.h"

namespace kpengine{
    PrimitiveSceneProxy::PrimitiveSceneProxy(Transform3f transform):transfrom_(transform)
    {
    }
 
    void PrimitiveSceneProxy::Initialize()
    {
        
    }
 
    void PrimitiveSceneProxy::UpdateViewPosition(float* view_pos)
    {
        view_pos_ = view_pos;
    }
    void PrimitiveSceneProxy::UpdateLightSpace(float* light_space)
    {
        light_space_ = light_space;
    }

    void PrimitiveSceneProxy::UpdateTransform(const Transform3f& new_transfrom)
    {
        transfrom_ = new_transfrom;
    }

    AABB PrimitiveSceneProxy::GetAABB()
    {
        return AABB();
    }
}