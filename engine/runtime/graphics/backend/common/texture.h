#ifndef KPENGINE_RUNTIME_GRAPHICS_TEXTURE_H
#define KPENGINE_RUNTIME_GRAPHICS_TEXTURE_H

#include <cstdint>
#include "enum.h"

namespace kpengine::graphics
{
    using TextureImage = void*;
    using TextureView = void*;
    using TextureSampler = void*;

    struct TextureDesc
    {
        uint32_t width;
        uint32_t height;
        uint32_t depth;
        uint32_t mip_levels = 1;
        uint32_t array_layers = 1;
        uint32_t sample_count = 1;
        TextureFormat format = TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM;
        TextureType type = TextureType::TEXTURE_TYPE_2D;
        TextureUsage usage = TextureUsage::TEXTURE_USAGE_SAMPLE;
    };

    struct TextureResource{
        TextureImage image = nullptr;
        TextureView view = nullptr;
        TextureSampler sampler = nullptr;
    };

    class Texture{
    public:
        virtual void Initialize(const TextureDesc& desc) = 0;
        virtual void Destroy() = 0;
    
        virtual TextureResource GetTextueHandle() const = 0;
    };
}

#endif