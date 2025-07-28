#include "scene_component.h"

namespace kpengine
{

    void SceneComponent::TickComponent(float delta_time)
    {
    }

    void SceneComponent::Initialize()
    {
        MarkTransformDirty();
    }

    void SceneComponent::MarkTransformDirty()
    {
        is_transform_dirty = true;
         if (!attach_parent_.expired())
            {
                world_transform_ = transform_ * attach_parent_.lock()->GetWorldTransform();
            }
            else
            {
                world_transform_ = transform_;
            }

        for (auto &child : attach_children_)
        {
            child->MarkTransformDirty();
        }
    }

    Transform3f SceneComponent::GetWorldTransform() const
    {
        return world_transform_;
    }

    Vector3f SceneComponent::GetWorldLocation() const
    {
        return GetWorldTransform().position_;
    }

    Vector3f SceneComponent::GetWorldScale() const
    {
        return GetWorldTransform().scale_;
    }

    Rotatorf SceneComponent::GetWorldRotation() const
    {
        return GetWorldTransform().rotator_;
    }

    void SceneComponent::SetRelativeLocation(const Vector3f &new_location)
    {
        if (transform_.position_ != new_location)
        {
            transform_.position_ = new_location;
            MarkTransformDirty();
        }
    }

    void SceneComponent::SetRelativeRotation(const Rotatorf &new_rotator)
    {
        if (transform_.rotator_ != new_rotator)
        {
            transform_.rotator_ = new_rotator;
            MarkTransformDirty();
        }
    }

    void SceneComponent::SetRelativeScale(const Vector3f &new_scale)
    {
        if (transform_.scale_ != new_scale)
        {
            transform_.scale_ = new_scale;
            MarkTransformDirty();
        }
    }

    void SceneComponent::SetRelativeTransform(const Transform3f &new_transform)
    {
        if(transform_ != new_transform)
        {
            transform_ = new_transform;
            MarkTransformDirty();
        }
    }

    void SceneComponent::AddChild(const std::shared_ptr<SceneComponent> &child)
    {
        // TODO Invoke some correspond
        if(child)
        {
            attach_children_.push_back(child);
            child->AttachToComponent(shared_from_this());
        }
    }

    void SceneComponent::AttachToComponent(const std::shared_ptr<SceneComponent> &target_comp)
    {
        if (target_comp == nullptr)
        {
            return;
        }
        attach_parent_ = target_comp;
        target_comp->AddChild(shared_from_this());
        MarkTransformDirty();
    }

    bool SceneComponent::RemoveChild(const std::shared_ptr<SceneComponent> &child)
    {
        auto it = std::find(attach_children_.begin(), attach_children_.end(), child);
        if (it != attach_children_.end())
        {
            attach_children_.erase(it);
            return true;
        }
        return false;
    }

    void SceneComponent::Detach()
    {
        if (!attach_parent_.expired())
        {
            attach_parent_.lock()->RemoveChild(shared_from_this());
        }
        attach_parent_.reset();
        MarkTransformDirty();
    }

    SceneComponent::~SceneComponent()
    {
        // TODO resource
    }
}