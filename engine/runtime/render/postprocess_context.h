#ifndef KPENGINE_RUNTIME_POSTPROCESS_CONTEXT_H
#define KPENGINE_RUNTIME_POSTPROCESS_CONTEXT_H

#include "math/math_header.h"

namespace kpengine{
    struct PostProcessContext{
        unsigned int g_object_id;
        Vector2f texel_size;
    };
}

#endif