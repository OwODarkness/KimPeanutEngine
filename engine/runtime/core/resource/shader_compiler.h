#ifndef KPENGINE_RUNTIME_RESOURCE_SHADER_COMPILER_H
#define KPENGINE_RUNTIME_RESOURCE_SHADER_COMPILER_H

#include <memory>
#include <vector>
#include <string>
#include "data/shader.h"
#include "base/type.h"
#include "base/graphics_type.h"

namespace kpengine::resource{
    struct ShaderCompileInput
    {
        std::string source; 
        std::string file_name;
        ShaderStage stage;
        ShaderFormat format;
    };

    class ShaderCompiler{
    public:
        virtual void Initialize(GraphicsAPIType api_type);
        virtual std::vector<uint8_t> Compile(const ShaderCompileInput& input ) = 0;
        virtual ~ShaderCompiler() = default;
    protected:
        GraphicsAPIType api;
    };
}

#endif