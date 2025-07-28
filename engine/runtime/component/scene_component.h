#ifndef KPENGINE_RUNTIME_SCENE_COMPONENT_H
#define KPENGINE_RUNTIME_SCENE_COMPONENT_H

#include <memory>
#include <vector>

#include "actor_component.h"
#include "runtime/core/math/math_header.h"
#include "runtime/core/system/delegate.h"


namespace kpengine{
    class SceneComponent: public ActorComponent, public std::enable_shared_from_this<SceneComponent>{
    public:
        
        virtual void TickComponent(float delta_time) override;
        virtual void Initialize() override;
        void AttachToComponent(const std::shared_ptr<SceneComponent>& child);
        void Detach();

        inline Transform3f GetRelativeTransform() const{return transform_;}
        inline Vector3f GetRelativeLocation() const{return transform_.position_;}
        inline Vector3f GetRelativeScale() const{return transform_.scale_;}
        inline Rotatorf GetRelativeRotation() const{return transform_.rotator_;}
        
        Transform3f GetWorldTransform() const;
        Vector3f GetWorldLocation() const;
        Vector3f GetWorldScale() const;
        Rotatorf GetWorldRotation() const;

        void SetRelativeLocation(const Vector3f& new_location);
        void SetRelativeRotation(const Rotatorf& new_rotator);
        void SetRelativeScale(const Vector3f& new_scale);
        void SetRelativeTransform(const Transform3f& new_transform);

        ~SceneComponent() override;
    protected:
        void AddChild(const std::shared_ptr<SceneComponent>& child);
        bool RemoveChild(const std::shared_ptr<SceneComponent>& child);
        void MarkTransformDirty();
    protected:
        Transform3f transform_;
        Transform3f world_transform_;
        bool is_transform_dirty{};
        std::weak_ptr<SceneComponent> attach_parent_;
        std::vector<std::shared_ptr<SceneComponent>> attach_children_;
    };
}

#endif