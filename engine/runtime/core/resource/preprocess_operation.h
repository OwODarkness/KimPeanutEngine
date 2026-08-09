#ifndef KPENGINE_RUNTIME_RESOURCE_PREPROCESS_OPERATION_H
#define KPENGINE_RUNTIME_RESOURCE_PREPROCESS_OPERATION_H

#include <shaderc/shaderc.hpp>
#include "shader_operation.h"

namespace kpengine::resource
{
    // The Preprocess stage of the shader pipeline. OpenGL's artifact is the final
    // assembled GLSL source — not binary bytes — so instead of compiling to SPIR-V
    // this stage expands includes, injects defines, and rewrites context.source
    // with the result. Purely CPU-side: shaderc needs no GL context. The compiled
    // source is handed to glShaderSource by the GL backend at runtime.
    class PreprocessOperation : public ShaderOperation
    {
    public:
        void Initialize(GraphicsAPIType api_type) override;
        ShaderProcessPhase GetPhase() const override { return ShaderProcessPhase::Preprocess; }
        const char* GetName() const override { return "preprocess"; }
        bool Run(ShaderProcessContext& context) override;
    private:
        shaderc::Compiler compiler_;
    };
}

#endif
