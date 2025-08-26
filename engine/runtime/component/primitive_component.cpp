#include "primitive_component.h"
#include "render/primitive_scene_proxy.h"

namespace kpengine{
    void PrimitiveComponent::Initialize()
    {
        SceneComponent::Initialize();
        if(scene_proxy_)
        {
            
        }
    }

    void PrimitiveComponent::TickComponent(float delta_time)
    {
        SceneComponent::TickComponent(delta_time);
        if(scene_proxy_)
        {
            scene_proxy_->UpdateTransform(GetWorldTransform());
        }
    }

    PrimitiveComponent::~PrimitiveComponent()
    {
        UnRegisterSceneProxy();
        scene_proxy_.reset();
    }

    void PrimitiveComponent::UnRegisterSceneProxy()
    {

    }
}