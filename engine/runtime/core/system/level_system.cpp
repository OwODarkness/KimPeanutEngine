#include "level_system.h"
#include <iostream>
#include "runtime/game_framework/level.h"

namespace kpengine{
    LevelSystem::LevelSystem():
    level_(std::make_unique<Level>())
    {
        
    }

    void LevelSystem::Initialize()
    {
        level_->Initialize();
        std::cout << "level system init successfully\n";
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