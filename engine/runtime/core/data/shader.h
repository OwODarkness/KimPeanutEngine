#ifndef KPENGINE_RUNTIME_DATA_SHADER_H
#define KPENGINE_RUNTIME_DATA_SHADER_H

#include <string>
#include <vector>
#include "base/graphics_type.h"
#include "base/type.h"

namespace kpengine::data{
    struct ShaderData{
        ShaderStage stage;
        // Which API the artifact was baked for. UNKNOW while the shader is
        // still source-only; the compiler sets it at bake time so consumers
        // can tell SPIR-V (byte_code) from GLSL source (source) without guessing.
        GraphicsAPIType api = GraphicsAPIType::GRAPHICS_API_UNKNOW;
        // Binary artifact (SPIR-V), Vulkan. Binary only — text never goes here.
        std::vector<uint8_t> byte_code;
        // Text artifact (preprocessed GLSL), OpenGL. Only set when api == OPENGL.
        std::string source;
        std::string entry = "main";
    };
}

#endif