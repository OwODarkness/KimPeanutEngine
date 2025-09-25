#ifndef KPENGINE_RUNTIME_GRAPHICS_MESH_RESOURCE_H
#define KPENGINE_RUNTIME_GRAPHICS_MESH_RESOURCE_H

#include <vector>
#include "mesh_section.h"
#include "vertex.h"

namespace kpengine::graphics{
    struct MeshData{
        std::vector<Vertex> vertices;
        std::vector<uint32_t> indices;
        std::vector<MeshSection> sections;
    };
}

#endif