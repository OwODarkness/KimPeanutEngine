#include "render_mesh_resource.h"

#include <iostream>

namespace kpengine{
    void RenderMeshResource::Debug()
    {
        for(unsigned int i = 0;i<mesh_sections_.size();i++)
        {
            std::cout << "[section " << i << "]:\n";
            std::cout << "face: " << mesh_sections_[i].face_count << "\n";
            std::cout << "range: " << mesh_sections_[i].index_start << " -- " << mesh_sections_[i].index_start + mesh_sections_[i].index_count - 1 << "\n"; 
        }
    }
}