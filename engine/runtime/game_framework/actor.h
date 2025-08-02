#ifndef KPENGINE_RUNTIME_GAME_FRAMEWORK_ACTOR_H
#define KPENGINE_RUNTIME_GAME_FRAMEWORK_ACTOR_H

#include <vector>
#include <string>
#include <memory>

#include "runtime/object/object.h"
#include "runtime/core/math/math_header.h"

class ActorComponent;
namespace kpengine{

    class ActorComponent;
    class SceneComponent;

    class Actor: public Object{
    public:
        Actor();
        ~Actor();
        virtual void Initialize();
        virtual void Tick(float delta_time);
        Vector3f GetActorLocation() const;
        Vector3f GetActorScale() const;
        Rotatorf GetActorRotation() const;
        Transform3f GetActorTransform() const;
        SceneComponent* GetRootComponent(){return root_component_.get();}

        void SetActorLocation(const Vector3f& new_location);
        void SetActorRotation(const Rotatorf& new_rotation);
        void SetActorScale(const Vector3f& new_scale);
        void SetTransform(const Transform3f& new_transform);

        void AddRelativeOffset(const Vector3f& delta);

        std::string GetName() const{return name_;}
    protected:
        std::vector<std::shared_ptr<Actor>> children_;
        std::weak_ptr<Actor> owner_;
        std::vector<std::shared_ptr<ActorComponent>> actor_components_;
        std::shared_ptr<SceneComponent> root_component_;

        std::string name_;
    };

}

#endif