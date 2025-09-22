#ifndef KPENGINE_RUNTIME_GRAPHICS_IMAGE_LOADER_H
#define KPENGINE_RUNTIME_GRAPHICS_IMAGE_LOADER_H

#include <vector>
#include <string>
#include <cstdint>
#include "common/enum.h"

namespace kpengine::graphics{
    struct TextureData
    {
        uint32_t width = 0;
        uint32_t height = 0;
        TextureFormat format = TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
        std::string path;
        std::vector<uint8_t> pixels;
    };


    class ImageLoader{
    public:
        static bool ReadFromFile(const std::string& path, TextureData& data);
    };
}

#endif