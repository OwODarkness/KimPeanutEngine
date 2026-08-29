#include "gameplay/actor/actor.h"

#include "gameplay/component/actor_component.h"
#include "gameplay/component/scene_component.h"

namespace kpengine::gameplay
{
    Actor::Actor(ActorHandle handle, render::IRenderableSourceSink *source_sink,
                 render::ILightSourceSink *light_source_sink)
        : handle_(handle), source_sink_(source_sink), light_source_sink_(light_source_sink)
    {
    }

    Actor::~Actor() = default;

    bool Actor::SetRootComponent(SceneComponent *component)
    {
        if (state_ != ActorState::Constructed || component == nullptr)
        {
            return false;
        }

        for (const std::unique_ptr<ActorComponent> &owned_component : components_)
        {
            if (owned_component.get() == component)
            {
                root_component_ = component;
                return true;
            }
        }
        return false;
    }

    bool Actor::Initialize()
    {
        if (state_ != ActorState::Constructed)
        {
            return false;
        }

        state_ = ActorState::Initialized;
        for (const std::unique_ptr<ActorComponent> &component : components_)
        {
            component->Initialize();
        }
        return true;
    }

    bool Actor::Activate()
    {
        if (state_ != ActorState::Initialized && state_ != ActorState::Inactive)
        {
            return false;
        }

        state_ = ActorState::Active;
        for (const std::unique_ptr<ActorComponent> &component : components_)
        {
            component->Activate();
        }
        return true;
    }

    bool Actor::Deactivate()
    {
        if (state_ != ActorState::Active)
        {
            return false;
        }

        state_ = ActorState::Inactive;
        for (auto it = components_.rbegin(); it != components_.rend(); ++it)
        {
            (*it)->Deactivate();
        }
        return true;
    }

    void Actor::Tick(float delta_time)
    {
        if (state_ != ActorState::Active)
        {
            return;
        }

        for (const std::unique_ptr<ActorComponent> &component : components_)
        {
            component->Tick(delta_time);
        }
    }

    void Actor::Destroy()
    {
        if (state_ == ActorState::Destroyed)
        {
            return;
        }

        (void)Deactivate();
        state_ = ActorState::Destroyed;
    }

    void Actor::AddComponentInternal(std::unique_ptr<ActorComponent> component)
    {
        component->SetOwner(this);
        components_.push_back(std::move(component));
    }
}
