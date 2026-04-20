#ifndef KPENGINE_RUNTIME_RESOURCE_SPIRV_COMPILER_H
#define KPENGINE_RUNTIME_RESOURCE_SPIRV_COMPILER_H

#include "shader_compiler.h"
#include <shaderc/shaderc.hpp>

namespace kpengine::resource{
    class SPIRVCompiler: public ShaderCompiler{
    public:
        void Initialize(GraphicsAPIType api_type) override;
        std::vector<uint8_t> Compile(const ShaderCompileInput& input) override;
    private:
        shaderc_shader_kind MatchShaderKind(ShaderStage stage);
    private:
        shaderc::Compiler compiler;
        shaderc::CompileOptions options;
    };
}

#endif