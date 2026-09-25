#include "spirv_compiler.h"
#include "shaderc_util.h"
#include "log/logger.h"
namespace kpengine::resource
{
    void SPIRVCompiler::Initialize(GraphicsAPIType api_type)
    {
        ShaderCompiler::Initialize(api_type);
        options.SetOptimizationLevel(shaderc_optimization_level_performance);
        // Ray-tracing shader stages are exposed by VK_KHR_ray_tracing_pipeline
        // through the Vulkan 1.2/SPIR-V 1.4 target; shaderc's default target is
        // too old and rejects GL_EXT_ray_tracing before validation can run.
        options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_2);
        options.SetTargetSpirv(shaderc_spirv_version_1_4);
    }

    std::vector<uint8_t> SPIRVCompiler::Compile(const ShaderCompileInput &input)
    {
        shaderc::SpvCompilationResult result{};
        shaderc_shader_kind shader_kind = MatchShaderKind(input.stage);

        if (input.format == ShaderFormat::SHADER_FORMAT_GLSL)
        {
            // Copy the shared options (optimization level, etc.) and layer the
            // per-shader macros on top, so defines never leak between compiles.
            shaderc::CompileOptions local_options = options;
            AddMacroDefinitions(local_options, input.defines);
            result = compiler.CompileGlslToSpv(input.source, shader_kind, input.file_name.c_str(), local_options);

            if(result.GetCompilationStatus() != shaderc_compilation_status_success)
            {
                std::string msg = result.GetErrorMessage();
                KP_LOG("SPIRVCompilerLog", LOG_LEVEL_ERROR, "Failed to compile %s to spirv, %s", input.file_name.c_str(), msg.c_str());

                return {};
            }
            size_t count = result.end() - result.begin();
            std::vector<uint8_t> out(count * 4);
            memcpy(out.data(), result.begin(), out.size());
            return out;
        }

        return {};
    }

}
