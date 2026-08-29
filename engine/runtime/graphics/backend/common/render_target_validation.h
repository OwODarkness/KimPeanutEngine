#ifndef KPENGINE_RUNTIME_GRAPHICS_RENDER_TARGET_VALIDATION_H
#define KPENGINE_RUNTIME_GRAPHICS_RENDER_TARGET_VALIDATION_H

#include <string>

#include "pipeline_types.h"
#include "render_target.h"

namespace kpengine::graphics
{
    // Validates the API-neutral target description before a backend allocates
    // native objects. `error` is optional because creation failures are
    // represented by an invalid handle.
    bool ValidateRenderTargetDesc(const RenderTargetDesc &desc, std::string *error = nullptr);

    // Validates that a pipeline can record into a target without a backend
    // state mismatch: color attachment count and formats, sample count, and
    // (when the pipeline declares one) the depth attachment format. A pipeline
    // with UNKNOW depth is compatible with any target depth. D2 currently
    // accepts single-sample targets only; multisample resolve is a later
    // contract.
    bool ValidateRenderTargetPipelineCompatibility(const RenderTargetDesc &target,
                                                   const PipelineDesc &pipeline,
                                                   std::string *error = nullptr);
}

#endif
