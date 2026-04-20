#ifndef KPENGINE_RUNTIME_ASSET_SHADER_H
#define KPENGINE_RUNTIME_ASSET_SHADER_H

#include <memory>
#include "data/shader.h"


namespace kpengine::asset{

using ShaderData = kpengine::data::ShaderData;

enum class ShaderStatus {
    Uncompiled, 
    Compiling,  
    Ready       
};


struct ShaderMeta{
    std::string file = "";
    ShaderStage stage = ShaderStage::SHADER_STAGE_UNKNOW;
    std::string entry = "main";
};

struct ShaderResource{
    std::shared_ptr<ShaderData> resource;
    ShaderMeta meta;
    ShaderFormat format;
    ShaderStatus status;
    ShaderResource():
    format(ShaderFormat::Unknown),
    status(ShaderStatus::Uncompiled){}
};
}


#endif