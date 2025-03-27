#ifndef KPENGINE_RUNTIME_GAME_FRAMEWORK_ACTOR_H
#define KPENGINE_RUNTIME_GAME_FRAMEWORK_ACTOR_H

#include <vector>
#include <memory>
#include "runtime/object/object.h"

class ActorComponent;
namespace kpengine{

    class Actor: public Object{
    public:
        void Tick(float delta_time);
    private:
        std::vector<Actor*> children_;
        std::weak_ptr<Actor> owner_;
    };

}

#endif