#include "actor.h"
#include "runtime/component/actor_component.h"
#include "runtime/component/scene_component.h"

namespace kpengine{
    Actor::Actor():
    children_({}),
    actor_components_({}),
    root_component_(nullptr)
    {

    }

    void Actor::Tick(float delta_time)
    {
        
    }

    Vector3f Actor::GetActorLocation() const
    {
        return root_component_ ? root_component_->GetWorldLocation() : Vector3f();
    }

    Vector3f Actor::GetActorScale() const
    {
        return root_component_ ? root_component_->GetWorldScale() : Vector3f();
    }

    Rotator3f Actor::GetActorRotation() const
    {
        return root_component_ ? root_component_->GetWorldRotation() : Rotator3f();
    }

    Transform3f Actor::GetActorTransform() const
    {
        return root_component_ ? root_component_->GetWorldTransform() : Transform3f();
    }

    Actor::~Actor()
    {
        
    }
}