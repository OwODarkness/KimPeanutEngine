#include "scene_component.h"

namespace kpengine{

    
    void SceneComponent::TickComponent(float delta_time)
    {
        
    }
    
    void SceneComponent::Initialize()
    {
        
    }

    Vector3f SceneComponent::GetWorldLocation() const
    {
        if(attach_parent_)
        {
            return transform_.position_ + attach_parent_->GetWorldLocation();

        }
        else
        {
            return transform_.position_;
        }
    }

    Vector3f SceneComponent::GetWorldScale() const
    {
        if(attach_parent_)
        {
            return transform_.scale_ * attach_parent_->GetWorldScale();

        }
        else
        {
            return transform_.scale_;
        }
    }

    Rotator3f SceneComponent::GetWorldRotation() const
    {
        if(attach_parent_)
        {
            return transform_.rotator_ + attach_parent_->GetWorldRotation();
        }
        else
        {
            return transform_.rotator_;
        }
    }

    Transform3f SceneComponent::GetWorldTransform() const
    {
        if(attach_parent_)
        {
            return Transform3f(
                GetWorldLocation(),
                GetWorldRotation(),
                GetWorldScale()
            );
        }
        else
        {
            return transform_;
        }
    }

    void SceneComponent::SetRelativeLocation(const Vector3f& new_location)
    {
        transform_.position_ = new_location;
    }

    void SceneComponent::SetRelativeRotation(const Rotator3f& new_rotation)
    {
        transform_.rotator_ = new_rotation;
    }

    void SceneComponent::SetRelativeScale(const Vector3f& new_scale)
    {
        transform_.scale_ = new_scale;
    }

    void SceneComponent::AddChild(SceneComponent* child)
    {
        //TODO Invoke some correspond 
        attach_children_.push_back(child);
    }

    void SceneComponent::AttachToComponent(SceneComponent* target_comp)
    {
        if(target_comp == nullptr)
        {
            return ;
        }
        attach_parent_ = target_comp;
        target_comp->AddChild(this);
    }

    SceneComponent::~SceneComponent()
    {

    }
}