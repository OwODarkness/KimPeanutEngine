#ifndef KPENGINE_RUNTIME_ASSET_MESH_RESOURCE_H
#define KPENGINE_RUNTIME_ASSET_MESH_RESOURCE_H

#include <memory>
#include "data/mesh.h"

namespace kpengine::asset
{
    using MeshData = kpengine::data::MeshData;
    using MeshSection = kpengine::data::MeshSection;
    using Vertex = kpengine::data::Vertex;
    using VertexHash = kpengine::data::VertexHash;
    struct MeshResource
    {
        std::shared_ptr<MeshData> data;
        uint32_t face_count;
        uint32_t vertex_count;

        MeshResource() : data(std::make_shared<MeshData>()) {}
    };
}

#endif