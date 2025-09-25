#include "opengl_mesh.h"

namespace kpengine::graphics
{
    MeshResource OpenglMesh::GetMeshHandle() const
    {
        return {&resource_};
    }
    void OpenglMesh::Initialize(const GraphicsContext &context, const MeshData &data)
    {
        
    }
    void OpenglMesh::Destroy(const GraphicsContext &context)
    {
    }
}