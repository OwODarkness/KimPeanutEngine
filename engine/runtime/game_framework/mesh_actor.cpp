#include "mesh_actor.h"
#include "runtime/component/mesh_component.h"
namespace kpengine{
    MeshActor::MeshActor(const std::string& relative_path):Actor()
    {
        root_component_ = std::make_shared<MeshComponent>(relative_path);    
    }

}