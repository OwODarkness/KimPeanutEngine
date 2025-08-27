#ifndef KPENGINE_RUNTIME_GRAPHICS_TEXTURE_H
#define KPENGINE_RUNTIME_GRAPHICS_TEXTURE_H

#include <cstdint>
#include "enum.h"

namespace kpengine
{

    struct TextureDesc
    {
        uint32_t width;
        uint32_t height;
        uint32_t depth;
        uint32_t mip_levels;
        uint32_t array_layers;
        uint32_t sample_count;
        TextureFormat format = TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM;
        TextureType type = TextureType::TEXTURE_TYPE_2D;
        TextureUsage usage = TextureUsage::TEXTURE_USAGE_SAMPLE;
    };


}

#endif