#ifndef KPENGINE_RUNTIME_ASSET_SHADER_MATA_H
#define KPENGINE_RUNTIME_ASSET_SHADER_META_H

#include <unordered_map>
#include <memory>
#include <vector>
#include "common.h"
#include "base/graphics_type.h"
#include "shader.h"

namespace kpengine::asset{

    struct ShaderMetaEntry
    {
        ShaderFormat format;
        AssetID asset;
    };

    struct ShaderMetaResource{
    public:
        void BindData(ShaderStage stage, ShaderFormat format, AssetID id);
        AssetID GetData(ShaderStage stage, ShaderFormat format);
        std::shared_ptr<struct ShaderResource> GetShader(ShaderStage stage, ShaderFormat format);
    private:
        std::unordered_map<ShaderStage, std::vector<ShaderMetaEntry>> datas;
    };

}

#endif