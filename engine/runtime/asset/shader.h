#ifndef KPENGINE_RUNTIME_ASSET_SHADER_H
#define KPENGINE_RUNTIME_ASSET_SHADER_H

#include <memory>
#include <string>
#include <vector>
#include "data/shader.h"


namespace kpengine::asset{

using ShaderData = kpengine::data::ShaderData;

enum class ShaderStatus {
    Uncompiled,
    Compiling,
    Ready,
    // A processing stage failed (e.g. compile); no usable data was produced.
    // Consumers must check status before reading ShaderResource::data.
    CompileFailed
};

// A program may provide source for the traditional per-material bindings and
// for the shared bindless texture table.  This is authoring identity, not a
// backend object; Render selects the active variant for a pipeline.
enum class ShaderProgramVariant : uint8_t
{
    Bound,
    Bindless,
};


// Identity of a single shader stage: where its source lives and how to compile
// it. Distinct from ShaderProgramResource, which is the multi-stage composition
// loaded from a .shader file.
struct ShaderStageDesc{
    std::string file = "";
    ShaderStage stage = ShaderStage::SHADER_STAGE_UNKNOW;
    std::string entry = "main";
    // Preprocessor macros, e.g. "MAX_LIGHTS 4". Declared in the .shader meta,
    // fed to the compiler, and part of the artifact cache key (GenerateShaderHash).
    std::vector<std::string> defines;
};

struct ShaderResource{
    std::shared_ptr<ShaderData> data;
    ShaderStageDesc desc;
    ShaderProgramVariant variant = ShaderProgramVariant::Bound;
    ShaderFormat format;
    ShaderStatus status;
    ShaderResource():
    format(ShaderFormat::Unknown),
    status(ShaderStatus::Uncompiled){}
};
}


#endif
