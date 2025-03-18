#ifndef KPENGINE_RUNTIME_SCENE_COMPONENT_H
#define KPENGINE_RUNTIME_SCENE_COMPONENT_H

#include "component.h"

namespace kpengine{
    class SceneComponent: public Component{
        virtual void TickComponent(float delta_time) override;
    };
}

#endif