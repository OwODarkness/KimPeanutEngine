#ifndef KPENGINE_RUNTIME_ASSET_SHADER_PROGRAM_H
#define KPENGINE_RUNTIME_ASSET_SHADER_PROGRAM_H

#include <unordered_map>
#include <memory>
#include <vector>
#include "common.h"
#include "base/graphics_type.h"
#include "shader.h"

namespace kpengine::asset{

    // A stage bound into a ShaderProgramResource: the source format plus the
    // AssetID of the ShaderResource it refers to.
    struct ShaderProgramEntry
    {
        ShaderFormat format;
        AssetID asset;
    };

    // The multi-stage composition loaded from a .shader file. Maps
    // (stage, source format) -> ShaderResource asset; distinct from
    // ShaderStageDesc, which is the per-stage identity.
    struct ShaderProgramResource{
    public:
        void BindData(ShaderStage stage, ShaderFormat format, AssetID id);
        AssetID GetData(ShaderStage stage, ShaderFormat format);
        std::shared_ptr<struct ShaderResource> GetShader(ShaderStage stage, ShaderFormat format);
    private:
        std::unordered_map<ShaderStage, std::vector<ShaderProgramEntry>> datas;
    };

}

#endif