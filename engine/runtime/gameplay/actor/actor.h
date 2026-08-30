#ifndef KPENGINE_RUNTIME_GAMEPLAY_ACTOR_ACTOR_H
#define KPENGINE_RUNTIME_GAMEPLAY_ACTOR_ACTOR_H

#include <memory>
#include <type_traits>
#include <utility>
#include <vector>

#include "gameplay/actor/actor_types.h"
#include "gameplay/component/actor_component.h"

namespace kpengine::render
{
    class ICameraSourceSink;
    class ILightSourceSink;
    class IRenderableSourceSink;
}

namespace kpengine::gameplay
{
    class SceneComponent;

    class Actor
    {
    public:
        explicit Actor(ActorHandle handle, render::IRenderableSourceSink *source_sink = nullptr,
                       render::ILightSourceSink *light_source_sink = nullptr,
                       render::ICameraSourceSink *camera_source_sink = nullptr);
        virtual ~Actor();

        Actor(const Actor &) = delete;
        Actor &operator=(const Actor &) = delete;
        Actor(Actor &&) = delete;
        Actor &operator=(Actor &&) = delete;

        ActorHandle GetHandle() const { return handle_; }
        ActorState GetState() const { return state_; }
        SceneComponent *GetRootComponent() const { return root_component_; }
        render::IRenderableSourceSink *GetRenderableSourceSink() const { return source_sink_; }
        render::ILightSourceSink *GetLightSourceSink() const { return light_source_sink_; }
        render::ICameraSourceSink *GetCameraSourceSink() const { return camera_source_sink_; }

        bool SetRootComponent(SceneComponent *component);

        template <typename ComponentT, typename... Args>
        ComponentT *AddComponent(Args &&...args)
        {
            static_assert(std::is_base_of_v<ActorComponent, ComponentT>,
                          "ComponentT must derive from ActorComponent");
            if (state_ != ActorState::Constructed)
            {
                return nullptr;
            }

            auto component = std::make_unique<ComponentT>(std::forward<Args>(args)...);
            ComponentT *const result = component.get();
            AddComponentInternal(std::move(component));
            return result;
        }

        template <typename ComponentT>
        ComponentT *FindComponent() const
        {
            static_assert(std::is_base_of_v<ActorComponent, ComponentT>,
                          "ComponentT must derive from ActorComponent");
            for (const std::unique_ptr<ActorComponent> &component : components_)
            {
                if (auto *const result = dynamic_cast<ComponentT *>(component.get()))
                {
                    return result;
                }
            }
            return nullptr;
        }

    private:
        friend class GameplayWorld;

        bool Initialize();
        bool Activate();
        bool Deactivate();
        void Tick(float delta_time);
        void Destroy();
        void AddComponentInternal(std::unique_ptr<ActorComponent> component);

        ActorHandle handle_;
        ActorState state_ = ActorState::Constructed;
        std::vector<std::unique_ptr<ActorComponent>> components_;
        SceneComponent *root_component_ = nullptr;
        render::IRenderableSourceSink *source_sink_ = nullptr;
        render::ILightSourceSink *light_source_sink_ = nullptr;
        render::ICameraSourceSink *camera_source_sink_ = nullptr;
    };
}

#endif
