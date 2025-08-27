#ifndef KPENGINE_RUNTIME_WORLD_SYSTEM_H
#define KPENGINE_RUNTIME_WORLD_SYSTEM_H

#include<memory>
#include <unordered_map>



namespace kpengine{
    enum class WorldType{
        None,
        Game,
        Editor
    };
    class Level;
    class World;

    class WorldSystem{
    public:
        WorldSystem();
        ~WorldSystem();
        void Tick(float delta_time);
        void Initialize();
        std::weak_ptr<World> GetCurrentWorld() const{return current_world;}
    private:
        std::unordered_map<WorldType, std::shared_ptr<World>> worlds;

        WorldType current_world_type_;
        std::weak_ptr<World> current_world;

    };
}

#endif