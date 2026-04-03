#ifndef KPENGINE_RUNTIME_ASSET_MODEL_RESOURCE_H
#define KPENGINE_RUNTIME_ASSET_MODEL_RESOURCE_H

#include <unordered_map>
#include <memory>

#include "common.h"
namespace kpengine::asset
{
    struct ModelResource
    {
    public:
        void BindData(ModelGeometryType type, AssetID id);
        AssetID GetData(ModelGeometryType type);
        std::shared_ptr<struct MeshResource> GetMesh();
    private:
        std::unordered_map<ModelGeometryType, AssetID> datas;
    };
}

#endif