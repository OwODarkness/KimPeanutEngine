#include "primitive_component.h"
#include "runtime/render/primitive_scene_proxy.h"

namespace kpengine{
    void PrimitiveComponent::Initialize()
    {
        SceneComponent::Initialize();
    }

    void PrimitiveComponent::TickComponent(float delta_time)
    {
        SceneComponent::TickComponent(delta_time);
        scene_proxy_->UpdateTransform(GetWorldTransform());
    }

    PrimitiveComponent::~PrimitiveComponent()
    {
        UnRegisterSceneProxy();
        scene_proxy_.reset();
    }
}