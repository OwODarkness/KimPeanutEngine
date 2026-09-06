#ifndef KPENGINE_RUNTIME_ASSET_MODEL_RESOURCE_H
#define KPENGINE_RUNTIME_ASSET_MODEL_RESOURCE_H

#include <cstdint>
#include <unordered_map>
#include <memory>
#include <vector>

#include "common.h"
namespace kpengine::asset
{
    struct ModelResource
    {
    public:
        void BindData(ModelGeometryType type, AssetID id);
        AssetID GetData(ModelGeometryType type);
        std::shared_ptr<struct MeshResource> GetMesh();
        void BindMaterialDependencyIndices(std::vector<std::uint32_t> indices);
        const std::vector<std::uint32_t> &GetMaterialDependencyIndices() const noexcept;
    private:
        std::unordered_map<ModelGeometryType, AssetID> datas;
        std::vector<std::uint32_t> material_dependency_indices_;
    };
}

#endif
