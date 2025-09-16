#include "opengl_texture.h"

namespace kpengine::graphics
{
    void OpenglTexture::Initialize(GraphicsContext device, const TextureData& data, const TextureSettings& settings)
    {
        // if(desc.usage == TextureUsage::TEXTURE_USAGE_SAMPLE)
        // {
        //     glGenTextures(1, &resource_.image);
        //     glGenSamplers(1, &resource_.sampler);
        // }   
    }
    void OpenglTexture::Destroy(GraphicsContext device)
    {
        // glDeleteTextures(1, &resource_.image);
        // glDeleteSamplers(1, &resource_.sampler);
    }
}