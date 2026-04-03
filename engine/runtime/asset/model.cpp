#include "model.h"
#include <string>
#include <magic_enum/magic_enum.hpp>
#include "mesh.h"
#include "asset_manager.h"
#include "log/logger.h"

namespace kpengine::asset
{
    void ModelResource::BindData(ModelGeometryType type, AssetID id)
    {
        datas[type] = id;
    }
    AssetID ModelResource::GetData(ModelGeometryType type)
    {
        if(datas.find(type) == datas.end())
        {
            std::string name = std::string(magic_enum::enum_name(type));
            KP_LOG("ModelResourceLog", LOG_LEVEL_ERROR, "No desired ModelGeometryType [%s] found", name.c_str());
            return AssetID();
        }
        return datas[type] ;
    }

    std::shared_ptr<MeshResource> ModelResource::GetMesh()
    {
        return AssetManager::GetInstance().GetResource<MeshResource>(GetData(ModelGeometryType::KPMG_Mesh));
    }
    
}