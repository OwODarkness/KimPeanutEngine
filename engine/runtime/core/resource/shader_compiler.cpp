#include "shader_compiler.h"

namespace kpengine::resource{
    void ShaderCompiler::Initialize(GraphicsAPIType api_type)
    {
        api = api_type;
    }
}