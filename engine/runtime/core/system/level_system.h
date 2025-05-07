#ifndef KPENGINE_RUNTIME_LEVEL_SYSTEM_H
#define KPENGINE_RUNTIME_LEVEL_SYSTEM_H

#include<memory>

namespace kpengine{
    class Level;
    class Actor;

    class LevelSystem{
    public:
        LevelSystem();
        ~LevelSystem();
        void Tick(float delta_time);
        void Initialize();
        std::shared_ptr<Actor> GetActor(unsigned int index);    
    private:
        std::unique_ptr<Level> level_;
    };
}

#endif