#include "shader_program.h"
#include "asset_manager.h"
#include "shader.h"
namespace kpengine::asset
{
    void ShaderProgramResource::BindData(ShaderStage stage, ShaderFormat format, AssetID id)
    {
        auto &list = datas[stage];

        for (auto &entry : list)
        {
            if (entry.format == format)
            {
                entry.asset = id;
                return;
            }
        }

        list.emplace_back(ShaderProgramEntry{format, id});
    }
    AssetID ShaderProgramResource::GetData(ShaderStage stage, ShaderFormat format)
    {
        auto it = datas.find(stage);
        if (it == datas.end())
        {
            return AssetID();
        }

        for (const auto &entry : it->second)
        {
            if (entry.format == format)
            {
                return entry.asset;
            }
        }

        return AssetID();
    }
    std::shared_ptr<ShaderResource> ShaderProgramResource::GetShader(ShaderStage stage, ShaderFormat format)
    {
        AssetID id = GetData(stage, format);

        if (!id.IsValid())
        {
            return nullptr;
        }

        return AssetManager::GetInstance().GetResource<ShaderResource>(id);
    }
}