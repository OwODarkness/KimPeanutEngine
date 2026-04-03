#ifndef KPENGINE_RUNTIME_DATA_SHADER_H
#define KPENGINE_RUNTIME_DATA_SHADER_H

#include <string>
#include <vector>
#include "base/graphics_type.h"

namespace kpengine::data{
    struct ShaderData{
        ShaderStage stage;
        std::vector<uint32_t> spirv;
        std::string glsl;
        std::string entry = "main";
    };
}

#endif