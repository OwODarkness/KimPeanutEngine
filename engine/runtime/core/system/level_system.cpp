#include "level_system.h"
#include "runtime/game_framework/level.h"

namespace kpengine{
    LevelSystem::LevelSystem():
    level_(std::make_unique<Level>())
    {
  
    }

    void LevelSystem::Initialize()
    {
        level_->Initialize();
    }

    void LevelSystem::Tick(float delta_time)
    {
        level_->Tick(delta_time);
    }

    std::shared_ptr<Actor> LevelSystem::GetActor(unsigned int index)
    {
        return level_->GetActor(index);
    }

    LevelSystem::~LevelSystem()
    {
        level_.reset();
    }
}