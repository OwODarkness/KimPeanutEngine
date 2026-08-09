#include "shader_compiler.h"

namespace kpengine::resource{
    void ShaderCompiler::Initialize(GraphicsAPIType api_type)
    {
        api = api_type;
    }

    bool ShaderCompiler::Run(ShaderProcessContext& context)
    {
        ShaderCompileInput input;
        input.source = context.source;
        input.file_name = context.file_name;
        input.stage = context.stage;
        input.format = context.format;
        input.defines = context.defines;
        context.byte_code = Compile(input);
        return !context.byte_code.empty();
    }
}