#ifndef KPENGINE_RUNTIME_GRAPHICS_API_H
#define KPENGINE_RUNTIME_GRAPHICS_API_H

#include <cstdint>

namespace kpengine::graphics{

    struct BufferHandle{
        uint32_t id = UINT32_MAX;
        uint32_t generation = 0;

        bool IsValid() const
        {
            return id == UINT32_MAX;
        }
    };
}

#endif