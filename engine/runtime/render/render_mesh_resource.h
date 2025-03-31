#ifndef KPENGINE_RUNTIME_RENDER_MESH_RESOURCE_H
#define KPENGINE_RUNTIME_RENDER_MESH_RESOURCE_H

#include <vector>

#include "mesh_vertex.h"
#include "mesh_section.h"

namespace kpengine{
    class RenderMeshResource{
    public:
        friend class ModelLoader_V2;
        void Debug();
    private:
        std::vector<MeshVertex> vertex_buffer_;
        std::vector<unsigned int> index_buffer_;
        std::vector<MeshSection> mesh_sections_;
    private:
        unsigned int vao_;
        unsigned int vbo_;
    };
}
#endif
