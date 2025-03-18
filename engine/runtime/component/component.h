#ifndef KPENGINE_RUNTIME_COMPONENT_H
#define KPENIGNE_RUNTIME_COMPONENT_H

#include "object/object.h"

namespace kpengine{
    class Component : public Object{
    public:
        virtual void TickComponent(float delta_time) = 0;
    };
};

#endif //KEPNGINE_RUNTIME_COMPONENT_H