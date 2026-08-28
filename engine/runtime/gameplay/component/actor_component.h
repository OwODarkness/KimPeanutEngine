#ifndef KPENGINE_RUNTIME_GAMEPLAY_COMPONENT_ACTOR_COMPONENT_H
#define KPENGINE_RUNTIME_GAMEPLAY_COMPONENT_ACTOR_COMPONENT_H

namespace kpengine::gameplay
{
    class Actor;

    class ActorComponent
    {
    public:
        virtual ~ActorComponent() = default;

        Actor *GetOwner() const { return owner_; }

    protected:
        virtual void OnInitialize() {}
        virtual void OnActivate() {}
        virtual void OnDeactivate() {}
        virtual void OnTick(float delta_time) { (void)delta_time; }

    private:
        friend class Actor;

        void SetOwner(Actor *owner) { owner_ = owner; }
        void Initialize() { OnInitialize(); }
        void Activate() { OnActivate(); }
        void Deactivate() { OnDeactivate(); }
        void Tick(float delta_time) { OnTick(delta_time); }

        Actor *owner_ = nullptr;
    };
}

#endif
