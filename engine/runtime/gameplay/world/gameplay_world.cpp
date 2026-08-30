#include "gameplay/world/gameplay_world.h"

#include "gameplay/actor/actor.h"
#include "gameplay/controller/player_controller.h"

namespace kpengine::gameplay
{
    GameplayWorld::GameplayWorld(render::IRenderableSourceSink *source_sink,
                                 render::ILightSourceSink *light_source_sink,
                                 render::ICameraSourceSink *camera_source_sink)
        : source_sink_(source_sink), light_source_sink_(light_source_sink),
          camera_source_sink_(camera_source_sink)
    {
    }

    GameplayWorld::~GameplayWorld()
    {
        Clear();
    }

    ActorHandle GameplayWorld::CreateActor()
    {
        const ActorHandle handle = actor_handles_.Create();
        actors_.emplace(handle.id,
                        std::make_unique<Actor>(handle, source_sink_, light_source_sink_,
                                                camera_source_sink_));
        return handle;
    }

    Actor *GameplayWorld::FindActor(ActorHandle handle)
    {
        if (!actor_handles_.IsHandleValid(handle))
        {
            return nullptr;
        }

        const auto it = actors_.find(handle.id);
        return it != actors_.end() && it->second->GetHandle() == handle ? it->second.get() : nullptr;
    }

    const Actor *GameplayWorld::FindActor(ActorHandle handle) const
    {
        if (!actor_handles_.IsHandleValid(handle))
        {
            return nullptr;
        }

        const auto it = actors_.find(handle.id);
        return it != actors_.end() && it->second->GetHandle() == handle ? it->second.get() : nullptr;
    }

    bool GameplayWorld::InitializeActor(ActorHandle handle)
    {
        Actor *const actor = FindActor(handle);
        return actor && actor->Initialize();
    }

    bool GameplayWorld::ActivateActor(ActorHandle handle)
    {
        Actor *const actor = FindActor(handle);
        return actor && actor->Activate();
    }

    bool GameplayWorld::DeactivateActor(ActorHandle handle)
    {
        Actor *const actor = FindActor(handle);
        return actor && actor->Deactivate();
    }

    bool GameplayWorld::DestroyActor(ActorHandle handle)
    {
        Actor *const actor = FindActor(handle);
        if (!actor)
        {
            return false;
        }

        actor->Destroy();
        return actor_handles_.Destroy(handle);
    }

    PlayerController *GameplayWorld::CreateLocalPlayerController(
        input::InputSystem *input_system, const std::string &input_context_name)
    {
        if (local_player_controller_ != nullptr)
        {
            return nullptr;
        }

        auto controller = std::make_unique<PlayerController>(*this, input_system,
                                                              input_context_name);
        if (input_system != nullptr && !controller->BindInput())
        {
            return nullptr;
        }
        local_player_controller_ = std::move(controller);
        return local_player_controller_.get();
    }

    void GameplayWorld::Tick(float delta_time)
    {
        if (local_player_controller_ != nullptr)
        {
            local_player_controller_->Tick(delta_time);
        }
        for (auto &[id, actor] : actors_)
        {
            (void)id;
            actor->Tick(delta_time);
        }
        ReclaimDestroyedActors();
    }

    void GameplayWorld::SetLocalPlayerControllerInputEnabled(bool enabled)
    {
        if (local_player_controller_ != nullptr)
        {
            local_player_controller_->SetInputEnabled(enabled);
        }
    }

    void GameplayWorld::Clear()
    {
        local_player_controller_.reset();
        for (auto &[id, actor] : actors_)
        {
            (void)id;
            actor->Destroy();
            (void)actor_handles_.Destroy(actor->GetHandle());
        }
        actors_.clear();
    }

    void GameplayWorld::ReclaimDestroyedActors()
    {
        for (auto it = actors_.begin(); it != actors_.end();)
        {
            if (it->second->GetState() == ActorState::Destroyed)
            {
                it = actors_.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }
}
