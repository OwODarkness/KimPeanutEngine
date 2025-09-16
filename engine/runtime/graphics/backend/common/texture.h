#ifndef KPENGINE_RUNTIME_GRAPHICS_TEXTURE_H
#define KPENGINE_RUNTIME_GRAPHICS_TEXTURE_H

#include <memory>
#include <cstdint>
#include "enum.h"
#include "graphics_context.h"
#include "tool/imageloader.h"

namespace kpengine::graphics
{
    using TextureImage = void *;
    using TextureView = void *;
    using TextureSampler = void *;


    struct TextureSettings
    {
        uint32_t mip_levels = 1;
        uint32_t sample_count = 1;
        TextureType type = TextureType::TEXTURE_TYPE_2D;
        TextureFormat format = TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
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
        virtual void Initialize(GraphicsContext device, const TextureData& data, const TextureSettings& settings) = 0;
        virtual void Destroy(GraphicsContext device ) = 0;
        virtual TextureResource GetTextueHandle() const = 0;
    protected:
        
    };
}

#endif