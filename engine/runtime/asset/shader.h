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
    Ready       
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
    std::shared_ptr<ShaderData> resource;
    ShaderStageDesc desc;
    ShaderFormat format;
    ShaderStatus status;
    ShaderResource():
    format(ShaderFormat::Unknown),
    status(ShaderStatus::Uncompiled){}
};
}


#endif