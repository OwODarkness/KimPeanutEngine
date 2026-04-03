#ifndef KPENGINE_RUNTIME_TEXTURE_DATA_H
#define KPENGINE_RUNTIME_TEXTURE_DATA_H

#include <string>
#include <vector>
#include <cstdint>
#include "base/graphics_type.h"
namespace kpengine::data{
    struct TextureData
    {
        uint32_t width;
        uint32_t height;
        uint32_t depth = 1;
        uint32_t array_layers = 1;
        TextureFormat format = TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
        std::vector<uint8_t> pixels;

    };
}

#endif