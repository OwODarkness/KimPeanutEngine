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
        if(!attach_parent_.expired())
        {
            return transform_.position_ + attach_parent_.lock()->GetWorldLocation();

        }
        else
        {
            return transform_.position_;
        }
    }

    Vector3f SceneComponent::GetWorldScale() const
    {
        if(!attach_parent_.expired())
        {
            return transform_.scale_ * attach_parent_.lock()->GetWorldScale();

        }
        else
        {
            return transform_.scale_;
        }
    }

    Rotator3f SceneComponent::GetWorldRotation() const
    {
        if(!attach_parent_.expired())
        {
            return transform_.rotator_ + attach_parent_.lock()->GetWorldRotation();
        }
        else
        {
            return transform_.rotator_;
        }
    }

    Transform3f SceneComponent::GetWorldTransform() const
    {
        if(!attach_parent_.expired())
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


    void SceneComponent::SetRelativeTransform(const Transform3f& new_transform)
    {
        transform_ = new_transform;
    }

    void SceneComponent::AddChild(const std::shared_ptr<SceneComponent>& child)
    {
        //TODO Invoke some correspond 
        attach_children_.push_back(child);
    }

    void SceneComponent::AttachToComponent(const std::shared_ptr<SceneComponent>& target_comp)
    {
        if(target_comp == nullptr)
        {
            return ;
        }
        attach_parent_ = target_comp;
        target_comp->AddChild(shared_from_this());
    }

    bool SceneComponent::RemoveChild(const std::shared_ptr<SceneComponent>& child)
    {
        auto it = std::find(attach_children_.begin(), attach_children_.end(), child);
        if (it != attach_children_.end()) {
            attach_children_.erase(it);
            return true;
        }
        return false;
    }

    void SceneComponent::Detach()
    {
        if(!attach_parent_.expired())
        {
            attach_parent_.lock()->RemoveChild(shared_from_this());
        }
        attach_parent_.reset();
    }

    SceneComponent::~SceneComponent()
    {
        //TODO resource
    }
}