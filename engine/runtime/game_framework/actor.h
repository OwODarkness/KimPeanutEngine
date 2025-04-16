#ifndef KPENGINE_RUNTIME_GAME_FRAMEWORK_ACTOR_H
#define KPENGINE_RUNTIME_GAME_FRAMEWORK_ACTOR_H

#include <vector>
#include <memory>

#include "object.h"
#include "runtime/core/math/math_header.h"

class ActorComponent;
namespace kpengine{

    class ActorComponent;
    class SceneComponent;

    class Actor: public Object{
    public:
        Actor();
        ~Actor();
        virtual void Tick(float delta_time);
        Vector3f GetActorLocation() const;
        Vector3f GetActorScale() const;
        Rotator3f GetActorRotation() const;
        Transform3f GetActorTransform() const;
    protected:
        std::vector<std::shared_ptr<Actor>> children_;
        std::weak_ptr<Actor> owner_;
        std::vector<std::shared_ptr<ActorComponent>> actor_components_;
        std::shared_ptr<SceneComponent> root_component_;
    };

}

#endif