#ifndef KPENGINE_RUNTIME_ASSET_SHADER_PROGRAM_H
#define KPENGINE_RUNTIME_ASSET_SHADER_PROGRAM_H

#include <unordered_map>
#include <memory>
#include <vector>
#include "common.h"
#include "base/graphics_type.h"
#include "shader.h"

namespace kpengine::asset{

    enum class ShaderProgramVariant : uint8_t
    {
        Bound,
        Bindless,
    };

    // A stage bound into a ShaderProgramResource: the source format plus the
    // AssetID of the ShaderResource it refers to.
    struct ShaderProgramEntry
    {
        ShaderFormat format;
        ShaderProgramVariant variant = ShaderProgramVariant::Bound;
        AssetID asset;
    };

    // The multi-stage composition loaded from a .shader file. Maps
    // (stage, source format, variant) -> ShaderResource asset; distinct from
    // ShaderStageDesc, which is the per-stage identity.
    struct ShaderProgramResource{
    public:
        void BindData(ShaderStage stage, ShaderFormat format, AssetID id,
                      ShaderProgramVariant variant = ShaderProgramVariant::Bound);
        AssetID GetData(ShaderStage stage, ShaderFormat format,
                        ShaderProgramVariant variant = ShaderProgramVariant::Bound);
        std::shared_ptr<struct ShaderResource> GetShader(
            ShaderStage stage, ShaderFormat format,
            ShaderProgramVariant variant = ShaderProgramVariant::Bound);
        // Every bound stage, across all formats, as a flat list for ProcessShader.
        std::vector<std::shared_ptr<struct ShaderResource>> GatherShaders() const;
    private:
        std::unordered_map<ShaderStage, std::vector<ShaderProgramEntry>> datas;
    };

}

#endif
