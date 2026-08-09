#include "preprocess_operation.h"
#include "shaderc_util.h"
#include "log/logger.h"

namespace kpengine::resource
{
    void PreprocessOperation::Initialize(GraphicsAPIType /*api_type*/)
    {
        // shaderc's compiler needs no per-API setup.
    }

    bool PreprocessOperation::Run(ShaderProcessContext &context)
    {
        shaderc::CompileOptions options;
        AddMacroDefinitions(options, context.defines);

        shaderc::PreprocessedSourceCompilationResult result = compiler_.PreprocessGlsl(
            context.source, MatchShaderKind(context.stage), context.file_name.c_str(), options);

        if (result.GetCompilationStatus() != shaderc_compilation_status_success)
        {
            KP_LOG("PreprocessOperationLog", LOG_LEVEL_ERROR,
                   "Failed to preprocess %s, %s", context.file_name.c_str(),
                   result.GetErrorMessage().c_str());
            return false;
        }

        context.source.assign(result.cbegin(), result.cend());
        return true;
    }
}
