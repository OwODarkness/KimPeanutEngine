#ifndef KPENGINE_RUNTIME_GRAPHICS_RESOURCE_BINDING_H
#define KPENGINE_RUNTIME_GRAPHICS_RESOURCE_BINDING_H

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

#include "api.h"

namespace kpengine::graphics
{
    using DynamicUniformOffsets = std::vector<uint32_t>;

    struct UniformBufferBinding
    {
        uint32_t set = 0;
        uint32_t binding = 0;
        BufferHandle buffer;
        size_t offset = 0;
        size_t range = 0;
    };

    struct SampledTextureBinding
    {
        uint32_t set = 0;
        uint32_t binding = 0;
        TextureHandle texture;
        SamplerHandle sampler;
    };

    using ResourceBinding = std::variant<UniformBufferBinding, SampledTextureBinding>;

    struct ResourceBindingSetDesc
    {
        uint32_t set = 0;
        std::vector<ResourceBinding> bindings;
        // Persistent sets are owned by the caller until explicitly destroyed.
        // Frame-local sets remain eligible for backend frame-slot recycling.
        bool persistent = false;
    };
}

#endif
