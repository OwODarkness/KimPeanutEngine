#ifndef KPENGINE_RUNTIME_GRAPHICS_MESH_SECTION_H
#define KPENGINE_RUNTIME_GRAPHICS_MESH_SECTION_H

#include <cstdint>
namespace kpengine::graphics{
    struct MeshSection{
       uint32_t index_start;
       uint32_t index_count;
       uint32_t material_index; 
    };
}

#endif