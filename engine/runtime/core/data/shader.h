#ifndef KPENGINE_RUNTIME_DATA_SHADER_H
#define KPENGINE_RUNTIME_DATA_SHADER_H

#include <string>
#include <vector>
#include "base/graphics_type.h"
#include "base/type.h"

namespace kpengine::data{
    struct ShaderData{
        ShaderStage stage;
        // Which API byte_code was baked for. UNKNOW while the shader is
        // still source-only; the compiler sets it at bake time so consumers
        // can tell SPIR-V from GLSL source without guessing.
        GraphicsAPIType api = GraphicsAPIType::GRAPHICS_API_UNKNOW;
        std::vector<uint8_t> byte_code;
        std::string entry = "main";
    };
}

#endif