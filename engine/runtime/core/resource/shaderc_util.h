#ifndef KPENGINE_RUNTIME_RESOURCE_SHADERC_UTIL_H
#define KPENGINE_RUNTIME_RESOURCE_SHADERC_UTIL_H

#include <shaderc/shaderc.hpp>
#include <string>
#include <vector>
#include "base/graphics_type.h"

namespace kpengine::resource
{
    // Map the engine's ShaderStage to the shaderc shader kind for a source.
    inline shaderc_shader_kind MatchShaderKind(ShaderStage stage)
    {
        switch (stage)
        {
        case ShaderStage::SHADER_STAGE_VERTEX:
            return shaderc_vertex_shader;
        case ShaderStage::SHADER_STAGE_FRAGMENT:
            return shaderc_fragment_shader;
        case ShaderStage::SHADER_STAGE_GEOMETRY:
            return shaderc_geometry_shader;
        case ShaderStage::SHADER_STAGE_COMPUTE:
            return shaderc_compute_shader;
        default:
            return shaderc_miss_shader;
        }
    }

    // Lay the per-shader macros ("NAME" or "NAME VALUE") onto shared options, so
    // the same options object can be copied per compile without leaking macros.
    inline void AddMacroDefinitions(shaderc::CompileOptions &options, const std::vector<std::string> &defines)
    {
        for (const auto &define : defines)
        {
            std::string name = define;
            std::string value;
            size_t sep = define.find_first_of(" \t=");
            if (sep != std::string::npos)
            {
                name = define.substr(0, sep);
                size_t start = define.find_first_not_of(" \t=", sep);
                if (start != std::string::npos)
                {
                    value = define.substr(start);
                }
            }
            options.AddMacroDefinition(name, value.empty() ? "1" : value);
        }
    }
}

#endif
