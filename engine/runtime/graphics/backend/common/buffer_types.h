#ifndef KPENGINE_RUNTIME_GRAPHICS_BUFFER_TYPES_H
#define KPENGINE_RUNTIME_GRAPHICS_BUFFER_TYPES_H

#include "enum.h"

namespace kpengine::graphics{
        struct VertexBindingDesc
    {
        uint32_t binding;
        uint32_t stride;
        bool per_instance;
    };

    struct VertexAttributionDesc
    {
        uint32_t location;
        uint32_t binding;
        VertexFormat format;
        uint32_t offset;
    };

}

#endif