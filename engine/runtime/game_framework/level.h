#ifndef KPENGINE_RUNTIME_LEVEL_H
#define KPNEGINE_RUNTIME_LEVEL_H

#include <memory>
#include <vector>

namespace kpengine{
    class Actor;

    class Level{
    public:
        Level();
        void Initialize();
        void Tick(float delta_time);
        bool AddActor(const std::shared_ptr<Actor>& actor);
        std::shared_ptr<Actor> GetActor(unsigned int index){return actors_.at(index);}

    private:
        std::vector<std::shared_ptr<Actor>> actors_;
    };
}


#endif