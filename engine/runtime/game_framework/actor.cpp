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
        if(root_component_)
        {
            root_component_->TickComponent(delta_time);
        }

        for(std::shared_ptr<ActorComponent>& actor_comp: actor_components_)
        {
            actor_comp->TickComponent(delta_time);
        }        
    }

    void Actor::Initialize()
    {
        if(root_component_)
        {
            root_component_->Initialize();
        }

        for(std::shared_ptr<ActorComponent>& actor_comp: actor_components_)
        {
            actor_comp->Initialize();
        }
    }

    Vector3f Actor::GetActorLocation() const
    {
        return root_component_ ? root_component_->GetWorldLocation() : Vector3f();
    }

    Vector3f Actor::GetActorScale() const
    {
        return root_component_ ? root_component_->GetWorldScale() : Vector3f();
    }

    Rotatorf Actor::GetActorRotation() const
    {
        return root_component_ ? root_component_->GetWorldRotation() : Rotatorf();
    }

    Transform3f Actor::GetActorTransform() const
    {
        return root_component_ ? root_component_->GetWorldTransform() : Transform3f();
    }

    void Actor::SetActorLocation(const Vector3f& new_location)
    {
        if(root_component_)
        {
            root_component_->SetRelativeLocation(new_location);
        }
    }
    void Actor::SetActorRotation(const Rotatorf& new_rotation)
    {
        if(root_component_)
        {
            root_component_->SetRelativeRotation(new_rotation);
        }
    }
    void Actor::SetActorScale(const Vector3f& new_scale)
    {
        if(root_component_)
        {
            root_component_->SetRelativeScale(new_scale);
        }
    }
    void Actor::SetTransform(const Transform3f& new_transform)
    {
        if(root_component_)
        {
            root_component_->SetRelativeTransform(new_transform);
        }
    }

    Actor::~Actor()
    {
        
    }
}