#include "world_system.h"
#include "level.h"
#include "world.h"
namespace kpengine{
    WorldSystem::WorldSystem()
    {
  
    }

    void WorldSystem::Initialize()
    {
        current_world_type_ = WorldType::Editor;
        worlds[WorldType::Editor] = std::make_shared<World>();
        current_world = worlds[WorldType::Editor];
        current_world.lock()->Load("Level0");
    }

    void WorldSystem::Tick(float delta_time)
    {
        if(!current_world.expired())
        {
            current_world.lock()->Tick(delta_time);
        }
    }


    WorldSystem::~WorldSystem()
    {
    }
}