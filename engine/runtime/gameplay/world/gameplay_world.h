#ifndef KPENGINE_RUNTIME_GAMEPLAY_WORLD_GAMEPLAY_WORLD_H
#define KPENGINE_RUNTIME_GAMEPLAY_WORLD_GAMEPLAY_WORLD_H

#include <memory>
#include <string>
#include <unordered_map>

#include "base/handle.h"
#include "gameplay/actor/actor.h"
#include "gameplay/actor/actor_types.h"

namespace kpengine::input
{
    class InputSystem;
}

namespace kpengine::render
{
    class ICameraSourceSink;
    class ILightSourceSink;
    class IRenderableSourceSink;
}

namespace kpengine::gameplay
{
    class PlayerController;

    class GameplayWorld
    {
    public:
        explicit GameplayWorld(render::IRenderableSourceSink *source_sink = nullptr,
                               render::ILightSourceSink *light_source_sink = nullptr,
                               render::ICameraSourceSink *camera_source_sink = nullptr);
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

        PlayerController *CreateLocalPlayerController(input::InputSystem *input_system,
                                                      const std::string &input_context_name =
                                                          "Gameplay");
        PlayerController *GetLocalPlayerController() const
        {
            return local_player_controller_.get();
        }

        // Applied on the game thread by Runtime's camera-control boundary.
        void SetLocalPlayerControllerInputEnabled(bool enabled);

        void Tick(float delta_time);
        void Clear();

    private:
        void ReclaimDestroyedActors();

        HandleSystem<ActorHandle> actor_handles_;
        std::unordered_map<uint32_t, std::unique_ptr<Actor>> actors_;
        std::unique_ptr<PlayerController> local_player_controller_;
        render::IRenderableSourceSink *source_sink_ = nullptr;
        render::ILightSourceSink *light_source_sink_ = nullptr;
        render::ICameraSourceSink *camera_source_sink_ = nullptr;
    };
}

#endif
