#ifndef KPENGINE_RUNTIME_SCENE_COMPONENT_H
#define KPENGINE_RUNTIME_SCENE_COMPONENT_H


#include <vector>

#include "actor_component.h"
#include "runtime/core/math/math_header.h"

namespace kpengine{
    class SceneComponent: public ActorComponent{
    public:
        SceneComponent() = default;
        virtual void TickComponent(float delta_time) override;
        void AttachToComponent(SceneComponent* target_comp);

        inline Vector3f GetRelativeLocation() const{return transform_.position_;}
        inline Vector3f GetRelativeScale() const{return transform_.scale_;}
        inline Rotator3f GetRelativeRotation() const{return transform_.rotator_;}
        inline Transform3f GetRelativeTransform() const{return transform_;}

        Vector3f GetWorldLocation() const;
        Vector3f GetWorldScale() const;
        Rotator3f GetWorldRotation() const;
        Transform3f GetWorldTransform() const;

        void SetRelativeLocation(const Vector3f& new_location);
        void SetRelativeRotation(const Rotator3f& new_rotation);
        void SetRelativeScale(const Vector3f& new_scale);


        ~SceneComponent();
    protected:
        void AddChild(SceneComponent* child);
    protected:
        Transform3f transform_;
        SceneComponent* attach_parent_;
        std::vector<ActorComponent*> attach_children_;
    };
}

#endif