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
                        ShaderProgramVariant variant = ShaderProgramVariant::Bound) const;
        std::shared_ptr<struct ShaderResource> GetShader(
            ShaderStage stage, ShaderFormat format,
            ShaderProgramVariant variant = ShaderProgramVariant::Bound);
        // Every stage from every variant, for explicit warm-all use only.
        std::vector<std::shared_ptr<struct ShaderResource>> GatherShaders() const;
        // The stage set selected by one pipeline.  Normal render resolution
        // processes this form so it does not compile an inactive variant.
        std::vector<std::shared_ptr<struct ShaderResource>> GatherShaders(
            ShaderProgramVariant variant) const;
    private:
        std::unordered_map<ShaderStage, std::vector<ShaderProgramEntry>> datas;
    };

}

#endif
