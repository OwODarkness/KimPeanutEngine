#include "scene_component.h"

namespace kpengine{

    void SceneComponent::TickComponent(float delta_time)
    {
        
    }

    void SceneComponent::AddChild(SceneComponent* child)
    {
        //TODO Invoke some correspond 
        attach_children_.push_back(child);
    }

    void SceneComponent::AttachToComponent(SceneComponent* target_comp)
    {
        if(target_comp == nullptr)
        {
            return ;
        }
        attach_parent_ = target_comp;
        target_comp->AddChild(this);
    }
}