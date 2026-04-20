#include "spirv_compiler.h"
#include "log/logger.h"
namespace kpengine::resource
{
    void SPIRVCompiler::Initialize(GraphicsAPIType api_type)
    {
        ShaderCompiler::Initialize(api_type);
        options.SetOptimizationLevel(shaderc_optimization_level_performance);
    }

    std::vector<uint8_t> SPIRVCompiler::Compile(const ShaderCompileInput &input)
    {
        shaderc::SpvCompilationResult result{};
        shaderc_shader_kind shader_kind = MatchShaderKind(input.stage);

        if (input.format == ShaderFormat::SHADER_FORMAT_GLSL)
        {
            result = compiler.CompileGlslToSpv(input.source, shader_kind, input.file_name.c_str());
            
            if(result.GetCompilationStatus() != shaderc_compilation_status_success)
            {
                std::string msg = result.GetErrorMessage();
                KP_LOG("SPIRVCompilerLog", LOG_LEVEL_ERROR, "Failed to compile %s to spirv, %s", input.file_name.c_str(), msg.c_str());
                throw std::runtime_error(msg);
            }
            size_t count = result.end() - result.begin();
            std::vector<uint8_t> out(count * 4);
            memcpy(out.data(), result.begin(), out.size());
            return out;
        }

        return {};
    }

    shaderc_shader_kind SPIRVCompiler::MatchShaderKind(ShaderStage stage)
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

}