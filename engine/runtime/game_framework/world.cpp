#include "world.h"
#include "level.h"
namespace kpengine{
    World::World()
    {

    }

    void World::Initialize()
    {
        is_level_loaded = false;
    }

    void World::Clear()
    {

    }

    void World::Tick(float delta_time)
    {
        if(!GetCurrentLevel().expired())
        {
            GetCurrentLevel().lock()->Tick(delta_time);
        }
    }

    void World::Load(const std::string& name)
    {
        std::shared_ptr<Level> level = std::make_shared<Level>();
        level->Initialize();
        levels_.insert({name, level});

        if(!is_level_loaded)
        {
            current_level_ = level;
        }
    }

    void World::UnLoad(const std::string& name)
    {
        levels_.erase(name);
    }
}