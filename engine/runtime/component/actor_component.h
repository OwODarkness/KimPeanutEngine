#ifndef KPENGINE_RUNTIME_ACTOR_COMPONENT_H
#define KPENGINE_RUNTIME_ACTOR_COMPONENT_H

#include "runtime/object/object.h"


namespace kpengine{

    class ActorComponent: public Object{
    public:
        virtual ~ActorComponent() = default;
        virtual void Initialize() = 0;
        virtual void TickComponent(float delta_time) = 0;
    };
};

#endif //KEPNGINE_RUNTIME_ACTOR_COMPONENT_H