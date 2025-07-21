#ifndef KPENGINE_RUNTIME_MESH_SECTION_H
#define KPENGINE_RUNTIME_MESH_SECTION_H

#include <memory>

namespace kpengine{

    class RenderMaterial;

    struct MeshSection{
        explicit MeshSection():index_start(0), index_count(0), face_count(0), material(nullptr){}
        explicit MeshSection(unsigned int start, unsigned count, std::shared_ptr<RenderMaterial> mat):
            index_start(start), index_count(count), material(mat)
            {
                face_count = count / 3;
            }
        explicit MeshSection(const MeshSection& mesh_section):
        index_start(mesh_section.index_start),
        index_count(mesh_section.index_count),
        face_count(mesh_section.face_count),
        material(mesh_section.material)
        {
        }
        unsigned int index_start;
        unsigned int index_count;
        unsigned int face_count;
        std::shared_ptr<RenderMaterial> material;
    };
}

#endif