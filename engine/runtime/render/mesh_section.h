#ifndef KPENGINE_RUNTIME_MESH_SECTION_H
#define KPENGINE_RUNTIME_MESH_SECTION_H

#include <memory>

namespace kpengine{

    class RenderMaterial;

    struct MeshSection{
        unsigned int index_start;
        unsigned int index_count;
        unsigned int face_count;
        std::shared_ptr<RenderMaterial> material;
    };
}

#endif