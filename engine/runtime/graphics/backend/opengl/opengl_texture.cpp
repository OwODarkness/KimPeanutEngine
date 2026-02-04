#include "opengl_texture.h"
#include "log/logger.h"
namespace kpengine::graphics
{
    void OpenglTexture::Initialize(GraphicsContext context, const TextureData &data, const TextureSettings &settings)
    {

        GLenum texture_gpu_type = ConvertToOpenglTextureType(settings.type);
        GLenum texture_gpu_format = ConvertToOpenglTextureInternalFormat(settings.format);
        GLenum texture_cpu_format = ConvertToOpenglTextureUploadFormat(settings.format);
        GLenum texture_cpu_type = ConvertToOpenglTextureUploadType(settings.format);

        glCreateTextures(texture_gpu_type, 1, &resource_.image);

        if (settings.type == TextureType::TEXTURE_TYPE_2D || settings.type == TextureType::TEXTURE_TYPE_CUBE)
        {
            glTextureStorage2D(resource_.image, settings.mip_levels, texture_gpu_format, data.width, data.height);
        }
        else if (settings.type == TextureType::TEXTURE_TYPE_3D)
        {
            glTextureStorage3D(resource_.image, settings.mip_levels, texture_gpu_format, data.width, data.height, data.depth);
        }

        if (!data.pixels.empty())
        {
            if (settings.type == TextureType::TEXTURE_TYPE_2D || settings.type == TextureType::TEXTURE_TYPE_CUBE)
            {
                glTextureSubImage2D(resource_.image, 0, 0, 0, data.width, data.height, texture_cpu_format, texture_cpu_type, data.pixels.data());
            }
            else if (settings.type == TextureType::TEXTURE_TYPE_3D)
            {
                glTextureSubImage3D(resource_.image, 0, 0, 0, 0, data.width, data.height, data.depth, texture_cpu_format, texture_cpu_type, data.pixels.data());
            }
        }

        if (settings.mip_levels > 1)
        {
            glGenerateTextureMipmap(resource_.image);
        }

    }
    void OpenglTexture::Destroy(GraphicsContext context)
    {
        if (resource_.image)
        {
            glDeleteTextures(1, &resource_.image);
        }
    }
}