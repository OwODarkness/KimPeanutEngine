#include "level_system.h"

#include "runtime/game_framework/level.h"

namespace kpengine{
    LevelSystem::LevelSystem()
    {
        
    }

    void LevelSystem::Initialize()
    {
        level_ = std::make_unique<Level>();
        level_->Initialize();
    }

    void LevelSystem::Tick(float delta_time)
    {
        level_->Tick(delta_time);
    }

    LevelSystem::~LevelSystem()
    {
        level_.reset();
    }
}