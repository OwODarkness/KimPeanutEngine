#ifndef KPENGINE_RUNTIME_SCENE_COMPONENT_H
#define KPENGINE_RUNTIME_SCENE_COMPONENT_H


#include <vector>
#include "actor_component.h"

namespace kpengine{
    class SceneComponent: public ActorComponent{
    public:
        virtual void TickComponent(float delta_time) override;
        void AttachToComponent(SceneComponent* target_comp);
    protected:
        void AddChild(SceneComponent* child);
    private:
        SceneComponent* attach_parent_;
        std::vector<ActorComponent*> attach_children_;
    };
}

#endif