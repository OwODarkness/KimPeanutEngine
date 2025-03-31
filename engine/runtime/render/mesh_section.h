#ifndef KPENGINE_RUNTIME_MESH_SECTION_H
#define KPENGINE_RUNTIME_MESH_SECTION_H


namespace kpengine{
    struct MeshSection{
        unsigned int index_start;
        unsigned int index_count;
        unsigned int face_count;
        unsigned int material_id;
    };
}

#endif