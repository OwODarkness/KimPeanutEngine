#ifndef KPENGINE_RUNTIME_RENDER_MESH_RESOURCE_H
#define KPENGINE_RUNTIME_RENDER_MESH_RESOURCE_H

#include <vector>

#include "mesh_vertex.h"
#include "mesh_section.h"

//renderable
namespace kpengine{
    class RenderMeshResource{
    public:
        unsigned int GetFaceNum() const;
    public:
        std::vector<MeshVertex> vertex_buffer_;
        std::vector<unsigned int> index_buffer_;
        std::vector<MeshSection> mesh_sections_;
    };
}
#endif
