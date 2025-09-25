#ifndef KPENGINE_RUNTIME_GRAPHICS_MESH_H
#define KPENGINE_RUNTIME_GRAPHICS_MESH_H

#include <vector>
#include "graphics_context.h"
#include "mesh_data.h"
namespace kpengine::graphics{

    struct MeshResource{
        const void* native;
    };

    class Mesh{
    public:
        virtual MeshResource GetMeshHandle() const = 0;
        virtual void Initialize(const GraphicsContext& context, const MeshData& data) = 0;
        virtual void Destroy(const GraphicsContext& context) = 0;
    };
}

#endif