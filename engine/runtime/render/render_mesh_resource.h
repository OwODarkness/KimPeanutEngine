#ifndef KPENGINE_RUNTIME_RENDER_MESH_RESOURCE_H
#define KPENGINE_RUNTIME_RENDER_MESH_RESOURCE_H

#include <vector>

#include "mesh_vertex.h"
#include "mesh_section.h"
#include "aabb.h"

//renderable
namespace kpengine{
    class RenderMeshResource{
    public:
        unsigned int GetFaceNum() const;
        void Initialize();
    public:
        AABB aabb_;
        std::vector<MeshVertex> vertex_buffer_;
        std::vector<unsigned int> index_buffer_;
        std::vector<MeshSection> mesh_sections_;
    private:
        void InitAABB();
    };
}
#endif
