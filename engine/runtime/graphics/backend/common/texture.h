#ifndef KPENGINE_RUNTIME_GRAPHICS_TEXTURE_H
#define KPENGINE_RUNTIME_GRAPHICS_TEXTURE_H

#include <memory>
#include <cstdint>
#include "enum.h"
#include "graphics_context.h"
#include "tool/image_loader.h"

namespace kpengine::graphics
{
    using TextureImage = void *;
    using TextureView = void *;


    struct TextureSettings
    {
        uint32_t mip_levels = 1;
        uint32_t sample_count = 1;
        TextureType type = TextureType::TEXTURE_TYPE_2D;
        TextureFormat format = TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;
        TextureUsage usage = TextureUsage::TEXTURE_USAGE_SAMPLE;
        ImageAspect aspect = ImageAspect::IMAGE_ASPECT_COLOR;
        };

    struct TextureResource
    {
        TextureImage image = nullptr;
        TextureView view = nullptr;
    };

    class Texture
    {
    public:
        virtual TextureResource GetTextueHandle() const = 0;
    protected:
        virtual void Initialize(GraphicsContext context, const TextureData& data, const TextureSettings& settings) = 0;
        virtual void Destroy(GraphicsContext context) = 0;
        
    private:
        friend class TextureManager; 
        
    };
}

#endif