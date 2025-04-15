#include "primitive_scene_proxy.h"

namespace kpengine{
    PrimitiveSceneProxy::PrimitiveSceneProxy(Transform3f transform):transfrom_(transform)
    {
    }
 
    void PrimitiveSceneProxy::Initialize()
    {
        
    }
 
    void PrimitiveSceneProxy::UpdateTransform(const Transform3f& new_transfrom)
    {
        transfrom_ = new_transfrom;
    }
}