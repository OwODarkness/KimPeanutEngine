#ifndef KPENGINE_RUNTIME_GRAPHICS_GRAPHICS_CAPABILITIES_H
#define KPENGINE_RUNTIME_GRAPHICS_GRAPHICS_CAPABILITIES_H

#include <cstdint>

namespace kpengine::graphics
{
    // Effective capabilities of the initialized common RHI path. A feature is
    // true only when the backend enables it and the common contract exposes it.
    struct GraphicsCapabilities
    {
        uint32_t max_sampled_textures_per_shader_stage = 0;
        bool bindless_textures = false;
    };
}

#endif
