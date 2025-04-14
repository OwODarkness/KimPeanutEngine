#include "primitive_scene_proxy.h"

namespace kpengine{
    void PrimitiveSceneProxy::UpdateTransform(const Transform3f& new_transfrom)
    {
        transfrom_ = new_transfrom;
    }
}