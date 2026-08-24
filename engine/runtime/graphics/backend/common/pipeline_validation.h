#ifndef KPENGINE_RUNTIME_GRAPHICS_PIPELINE_VALIDATION_H
#define KPENGINE_RUNTIME_GRAPHICS_PIPELINE_VALIDATION_H

#include <string>

#include "base/type.h"
#include "pipeline_types.h"

namespace kpengine::graphics
{
    // Validates the API-neutral contract before a backend allocates native objects.
    // `error` is optional because creation failures are represented by an invalid handle.
    bool ValidatePipelineDesc(const PipelineDesc &desc, GraphicsAPIType api,
                              std::string *error = nullptr);
}

#endif
