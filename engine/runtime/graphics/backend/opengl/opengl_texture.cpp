#include "opengl_texture.h"

namespace kpengine::graphics
{
    void OpenglTexture::Initialize(const TextureDesc &desc)
    {
        if(desc.usage == TextureUsage::TEXTURE_USAGE_SAMPLE)
        {
            glGenTextures(1, &resource_.image);
            glGenSamplers(1, &resource_.sampler);
        }   
    }
    void OpenglTexture::Destroy()
    {
        glDeleteTextures(1, &resource_.image);
        glDeleteSamplers(1, &resource_.sampler);
    }
}