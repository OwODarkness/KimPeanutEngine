#ifndef KPENGINE_RUNTIME_WORLD_H
#define KPENGINE_RUNTIME_WORLD_H

#include <string>
#include <unordered_map>
#include <memory>

namespace kpengine{


    class Level;

    class World{
    public:
        World();
        void Initialize();
        void Clear();
        void Tick(float delta_time);
        void Load(const std::string& name);
        void UnLoad(const std::string& name);

        std::weak_ptr<Level> GetCurrentLevel() const{return current_level_;}
    private:
        std::unordered_map<std::string, std::shared_ptr<Level>> levels_;
        std::weak_ptr<Level> current_level_;
        bool is_level_loaded{};
    };  
}

#endif