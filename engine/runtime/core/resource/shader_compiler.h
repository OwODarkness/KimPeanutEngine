#ifndef KPENGINE_RUNTIME_RESOURCE_SHADER_COMPILER_H
#define KPENGINE_RUNTIME_RESOURCE_SHADER_COMPILER_H

#include <memory>
#include <vector>
#include <string>
#include "data/shader.h"
#include "base/type.h"
#include "base/graphics_type.h"
#include "shader_operation.h"

namespace kpengine::resource{
    struct ShaderCompileInput
    {
        std::string source;
        std::string file_name;
        ShaderStage stage;
        ShaderFormat format;
        // Preprocessor macros, e.g. "MAX_LIGHTS 4"; fed to the compiler and
        // part of the artifact cache key.
        std::vector<std::string> defines;
    };

    // The compiler is the Compile stage of the shader processing pipeline.
    // SPIRVCompiler implements Compile(); the ShaderOperation seam lets the
    // processor drive it through the same Run/observer path as the future
    // preprocess/reflect/save stages.
    class ShaderCompiler : public ShaderOperation{
    public:
        virtual void Initialize(GraphicsAPIType api_type) = 0;
        virtual std::vector<uint8_t> Compile(const ShaderCompileInput& input ) = 0;
        ShaderProcessPhase GetPhase() const override { return ShaderProcessPhase::Compile; }
        const char* GetName() const override { return "compile"; }
        bool Run(ShaderProcessContext& context) override;
        virtual ~ShaderCompiler() = default;
    protected:
        GraphicsAPIType api;
    };
}

#endif