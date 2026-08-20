#ifndef KPENGINE_RUNTIME_GRAPHICS_RESOURCE_BINDING_H
#define KPENGINE_RUNTIME_GRAPHICS_RESOURCE_BINDING_H

#include <cstddef>
#include <cstdint>
#include <variant>
#include <vector>

#include "api.h"

namespace kpengine::graphics
{
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
    };
}

#endif
