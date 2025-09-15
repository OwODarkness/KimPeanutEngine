#ifndef KPENGINE_RUNTIME_GRAPHICS_TEXTURE_H
#define KPENGINE_RUNTIME_GRAPHICS_TEXTURE_H

#include <memory>
#include <cstdint>
#include "enum.h"

namespace kpengine::graphics
{
    using TextureImage = void *;
    using TextureView = void *;
    using TextureSampler = void *;

    using GraphicsDevice = void *;


    struct TextureSettings
    {
        uint32_t mip_levels = 1;
        uint32_t sample_count = 1;
        TextureType type = TextureType::TEXTURE_TYPE_2D;
        TextureUsage usage = TextureUsage::TEXTURE_USAGE_SAMPLE;
    };

    struct TextureResource
    {
        TextureImage image = nullptr;
        TextureView view = nullptr;
        TextureSampler sampler = nullptr;
    };

    class Texture
    {
    protected:
        virtual void Initialize(GraphicsDevice device = nullptr) = 0;
        virtual void Destroy(GraphicsDevice device = nullptr) = 0;
        virtual TextureResource GetTextueHandle() const = 0;
    protected:
        
    };
}

#endif