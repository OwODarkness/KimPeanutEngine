#ifndef KPENGINE_RUNTIME_ACTORCOMPONENT_H
#define KPENIGNE_RUNTIME_ACTORCOMPONENT_H

#include "object/object.h"

namespace kpengine{
    class ActorComponent : public Object{
    public:
        virtual void TickComponent(float delta_time) = 0;
    };
};

#endif //KEPNGINE_RUNTIME_COMPONENT_H