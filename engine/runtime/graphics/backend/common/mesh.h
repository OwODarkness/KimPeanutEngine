#ifndef KPENGINE_RUNTIME_GRAPHICS_MESH_H
#define KPENGINE_RUNTIME_GRAPHICS_MESH_H

#include <vector>
#include "base/base.h"
#include "data/mesh.h"

using MeshData = kpengine::data::MeshData;
using MeshSection = kpengine::data::MeshSection;
using Vertex = kpengine::data::Vertex;
using VertexHash = kpengine::data::VertexHash;

namespace kpengine::graphics{

    struct MeshResource{
        const void* native;
    };

    class Mesh{
    public:
        virtual ~Mesh() = default;
        virtual MeshResource GetMeshHandle() const = 0;
        virtual void Initialize(const GraphicsContext& context, const MeshData& data) = 0;
        virtual void Destroy(const GraphicsContext& context) = 0;
    };
}

#endif