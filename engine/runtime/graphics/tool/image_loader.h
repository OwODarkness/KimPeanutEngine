#ifndef KPENGINE_RUNTIME_GRAPHICS_IMAGE_LOADER_H
#define KPENGINE_RUNTIME_GRAPHICS_IMAGE_LOADER_H

#include <vector>
#include <string>
#include <cstdint>
#include "common/enum.h"
#include "common/texture.h"
namespace kpengine::graphics{



    class ImageLoader{
    public:
        static bool ReadFromFile(const std::string& path, TextureData& data);
    };
}

#endif