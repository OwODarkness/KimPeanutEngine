#ifndef KPENGINE_RUNTIME_GAMEPLAY_COMPONENT_SCENE_COMPONENT_H
#define KPENGINE_RUNTIME_GAMEPLAY_COMPONENT_SCENE_COMPONENT_H

#include <vector>

#include "gameplay/component/actor_component.h"
#include "math/math_header.h"

namespace kpengine::gameplay
{
    class SceneComponent : public ActorComponent
    {
    public:
        ~SceneComponent() override;

        const Transform3f &GetLocalTransform() const { return local_transform_; }
        const Transform3f &GetWorldTransform() const
        {
            UpdateWorldTransform();
            return world_transform_;
        }
        const Vector3f &GetLocalLocation() const { return local_transform_.position_; }
        const Vector3f &GetWorldLocation() const { return GetWorldTransform().position_; }
        const SceneComponent *GetAttachParent() const { return parent_; }
        bool IsTransformDirty() const { return transform_dirty_; }

        void SetLocalTransform(const Transform3f &transform);
        void SetLocalLocation(const Vector3f &location);
        void SetLocalRotation(const Rotatorf &rotation);
        void SetLocalScale(const Vector3f &scale);

        bool AttachTo(SceneComponent &parent);
        bool Detach();

    protected:
        void OnInitialize() override;
        void OnTick(float delta_time) override;

        void MarkTransformDirty();
        virtual void OnTransformChanged() {}

    private:
        bool IsAncestorOf(const SceneComponent &component) const;
        void UpdateWorldTransform() const;
        void RemoveChild(SceneComponent &child);

        Transform3f local_transform_;
        mutable Transform3f world_transform_;
        mutable bool transform_dirty_ = true;
        SceneComponent *parent_ = nullptr;
        std::vector<SceneComponent *> children_;
    };
}

#endif
