#ifndef KPENGINE_RUNTIME_GRAPHICS_CONTEXT_H
#define KPENGINE_RUNTIME_GRAPHICS_CONTEXT_H

#include "base/type.h"

namespace kpengine::graphics{
    struct GraphicsContext{
        GraphicsAPIType type;
        void* native = nullptr;
    };
}

#endif