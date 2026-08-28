#ifndef KPENGINE_RUNTIME_GAMEPLAY_WORLD_GAMEPLAY_WORLD_H
#define KPENGINE_RUNTIME_GAMEPLAY_WORLD_GAMEPLAY_WORLD_H

#include <memory>
#include <unordered_map>

#include "base/handle.h"
#include "gameplay/actor/actor.h"
#include "gameplay/actor/actor_types.h"

namespace kpengine::render
{
    class IRenderableSourceSink;
}

namespace kpengine::gameplay
{
    class GameplayWorld
    {
    public:
        explicit GameplayWorld(render::IRenderableSourceSink *source_sink = nullptr)
            : source_sink_(source_sink)
        {
        }
        ~GameplayWorld();

        GameplayWorld(const GameplayWorld &) = delete;
        GameplayWorld &operator=(const GameplayWorld &) = delete;
        GameplayWorld(GameplayWorld &&) = delete;
        GameplayWorld &operator=(GameplayWorld &&) = delete;

        ActorHandle CreateActor();
        Actor *FindActor(ActorHandle handle);
        const Actor *FindActor(ActorHandle handle) const;

        bool InitializeActor(ActorHandle handle);
        bool ActivateActor(ActorHandle handle);
        bool DeactivateActor(ActorHandle handle);
        bool DestroyActor(ActorHandle handle);

        void Tick(float delta_time);
        void Clear();

    private:
        void ReclaimDestroyedActors();

        HandleSystem<ActorHandle> actor_handles_;
        std::unordered_map<uint32_t, std::unique_ptr<Actor>> actors_;
        render::IRenderableSourceSink *source_sink_ = nullptr;
    };
}

#endif
