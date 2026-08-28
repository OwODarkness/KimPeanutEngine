#include "gameplay/component/scene_component.h"

#include <algorithm>

namespace kpengine::gameplay
{
    SceneComponent::~SceneComponent()
    {
        Detach();
        for (SceneComponent *const child : children_)
        {
            child->parent_ = nullptr;
            child->MarkTransformDirty();
        }
    }

    void SceneComponent::SetLocalTransform(const Transform3f &transform)
    {
        if (local_transform_ != transform)
        {
            local_transform_ = transform;
            MarkTransformDirty();
        }
    }

    void SceneComponent::SetLocalLocation(const Vector3f &location)
    {
        if (local_transform_.position_ != location)
        {
            local_transform_.position_ = location;
            MarkTransformDirty();
        }
    }

    void SceneComponent::SetLocalRotation(const Rotatorf &rotation)
    {
        if (local_transform_.rotator_ != rotation)
        {
            local_transform_.rotator_ = rotation;
            MarkTransformDirty();
        }
    }

    void SceneComponent::SetLocalScale(const Vector3f &scale)
    {
        if (local_transform_.scale_ != scale)
        {
            local_transform_.scale_ = scale;
            MarkTransformDirty();
        }
    }

    bool SceneComponent::AttachTo(SceneComponent &parent)
    {
        if (&parent == this || GetOwner() == nullptr || parent.GetOwner() != GetOwner() ||
            IsAncestorOf(parent))
        {
            return false;
        }
        if (parent_ == &parent)
        {
            return true;
        }

        Detach();
        parent_ = &parent;
        parent.children_.push_back(this);
        MarkTransformDirty();
        return true;
    }

    bool SceneComponent::Detach()
    {
        if (parent_ == nullptr)
        {
            return false;
        }

        parent_->RemoveChild(*this);
        parent_ = nullptr;
        MarkTransformDirty();
        return true;
    }

    void SceneComponent::OnInitialize()
    {
        MarkTransformDirty();
    }

    void SceneComponent::OnTick(float delta_time)
    {
        (void)delta_time;
        UpdateWorldTransform();
    }

    void SceneComponent::MarkTransformDirty()
    {
        transform_dirty_ = true;
        OnTransformChanged();
        for (SceneComponent *const child : children_)
        {
            child->MarkTransformDirty();
        }
    }

    bool SceneComponent::IsAncestorOf(const SceneComponent &component) const
    {
        for (const SceneComponent *ancestor = component.parent_; ancestor != nullptr;
             ancestor = ancestor->parent_)
        {
            if (ancestor == this)
            {
                return true;
            }
        }
        return false;
    }

    void SceneComponent::UpdateWorldTransform() const
    {
        if (!transform_dirty_)
        {
            return;
        }

        if (parent_ != nullptr)
        {
            parent_->UpdateWorldTransform();
            world_transform_ = parent_->world_transform_ * local_transform_;
        }
        else
        {
            world_transform_ = local_transform_;
        }
        transform_dirty_ = false;
    }

    void SceneComponent::RemoveChild(SceneComponent &child)
    {
        const auto it = std::find(children_.begin(), children_.end(), &child);
        if (it != children_.end())
        {
            children_.erase(it);
        }
    }
}
