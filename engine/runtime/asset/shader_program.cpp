#include "shader_program.h"
#include <unordered_set>
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

    std::vector<std::shared_ptr<ShaderResource>> ShaderProgramResource::GatherShaders() const
    {
        std::vector<std::shared_ptr<ShaderResource>> shaders;

        // Each stage/format pair is a distinct asset, but guard against a shared
        // stage being bound twice so ProcessShader never compiles the same unit twice.
        std::unordered_set<uint64_t> seen;
        for (const auto &[stage, entries] : datas)
        {
            (void)stage;
            for (const auto &entry : entries)
            {
                if (!entry.asset.IsValid() || !seen.insert(entry.asset.Pack()).second)
                {
                    continue;
                }

                auto shader = AssetManager::GetInstance().GetResource<ShaderResource>(entry.asset);
                if (shader)
                {
                    shaders.push_back(std::move(shader));
                }
            }
        }

        return shaders;
    }
}