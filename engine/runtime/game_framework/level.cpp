#include "level.h"

#include "runtime/game_framework/actor.h"

namespace kpengine{

    void Level::Initialize()
    {
        
    }

    void Level::Tick(float delta_time)
    {
        for(std::shared_ptr<Actor>& actor: actors_)
        {
            actor->Tick(delta_time);
        }
    }

    bool Level::AddActor(std::shared_ptr<Actor> actor)
    {
        if(actor == nullptr)
        {
            return false;
        }
        actors_.push_back(actor);
        return true;
    }
}