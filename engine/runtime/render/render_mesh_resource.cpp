#include "render_mesh_resource.h"
#include <glad/glad.h>

namespace kpengine{

unsigned int RenderMeshResource::GetFaceNum() const
{
    std::vector<MeshSection>::const_iterator iter;
    unsigned int count = 0;
    for(iter = mesh_sections_.cbegin() ; iter != mesh_sections_.cend();iter++)
    {
        count += iter->face_count;
    }
    return count;
}
void RenderMeshResource::Initialize()
{
    InitAABB();
}
void RenderMeshResource::InitAABB()
{
    for (std::vector<MeshVertex>::iterator iter = vertex_buffer_.begin(); iter != vertex_buffer_.end(); ++iter)
    {
        const Vector3f& cur_pos = iter->position;
        aabb_.Expand(cur_pos);
    }
}

}