#ifndef KPENGINE_RUNTIME_GRAPHICS_MESH_MANAGER_H
#define KPENGINE_RUNTIME_GRAPHICS_MESH_MANAGER_H

#include <memory>
#include <vector>
#include "api.h"
#include "mesh.h"
#include "graphics_context.h"
namespace kpengine::graphics
{
    struct MeshSlot
    {
        std::unique_ptr<Mesh> mesh;
    };

    class MeshManager
    {
    public:
        MeshHandle CreateMesh(const GraphicsContext &context, const MeshData &data);
        Mesh *GetMesh(MeshHandle handle);
        bool DestroyMesh(const GraphicsContext &context, MeshHandle Handle);

    private:
        MeshSlot *GetMeshSlot(MeshHandle handle);

    private:
        std::vector<MeshSlot> resources_;
        HandleSystem<MeshHandle> handle_system_;
    };
}

#endif