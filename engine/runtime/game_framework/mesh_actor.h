#ifndef KPENGINE_RUNTIME_MESH_ACTOR_H
#define KPENGINE_RUNTIME_MESH_ACTOR_H

#include <string>
#include <memory>
#include "actor.h"

namespace kpengine{

    class MeshActor: public Actor{
    public:
        MeshActor(const std::string& relative_path);
    
    };
}

#endif