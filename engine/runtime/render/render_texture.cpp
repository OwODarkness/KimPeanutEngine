#include "render_texture.h"

#include <filesystem>
#include <stb_image/image_helper.h>
#include <glad/glad.h>

#include "log/logger.h"
#include "config/path.h"

namespace kpengine
{
    RenderTexture::RenderTexture(const std::string &image_relative_path) : image_id_(image_relative_path)
    {
    }

    RenderTexture::~RenderTexture()
    {
        glDeleteTextures(1, &texture_handle_);
    }

    RenderTexture2D::RenderTexture2D(const std::string &image_path, bool is_color_texture) : RenderTexture(image_path), is_color_texture_(is_color_texture)
    {
    }

    RenderTexture2D::RenderTexture2D(const std::string &image_path, unsigned int handle) : RenderTexture(image_path)
    {
        texture_handle_ = handle;
    }

    bool RenderTexture2D::Initialize()
    {
        int width = 0, height = 0, nrChannels = 0;
        stbi_set_flip_vertically_on_load(true);

        std::string absoluteImagePath = GetAssetDirectory() + image_id_;
        unsigned char *imageData = stbi_load(absoluteImagePath.c_str(), &width, &height, &nrChannels, 0);

        if (!imageData)
        {
            KP_LOG("TextureLog", LOG_LEVEL_ERROR, "Failed to load texture from %s", absoluteImagePath.c_str());
            return false;
        }

        glGenTextures(1, &texture_handle_);
        glBindTexture(GL_TEXTURE_2D, texture_handle_);

        // Determine external format
        GLenum format = (nrChannels == 1) ? GL_RED : (nrChannels == 3) ? GL_RGB
                                                 : (nrChannels == 4)   ? GL_RGBA
                                                                       : 0;

        // Determine internal format
        GLenum internalFormat = 0;
        if (is_color_texture_)
        {
            // sRGB internal format for color textures
            internalFormat = (nrChannels == 3) ? GL_SRGB8 : (nrChannels == 4) ? GL_SRGB8_ALPHA8
                                                                              : GL_R8;
        }
        else
        {
            // Linear internal format for data textures
            internalFormat = (nrChannels == 1) ? GL_R8 : (nrChannels == 3) ? GL_RGB8
                                                     : (nrChannels == 4)   ? GL_RGBA8
                                                                           : GL_RGB8;
        }

        // Fix row alignment issues

        // Upload texture
        glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, imageData);
        glGenerateMipmap(GL_TEXTURE_2D);

        // Texture parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glBindTexture(GL_TEXTURE_2D, 0);
        stbi_image_free(imageData);
        return true;
    }

    RenderTexture2D::~RenderTexture2D() = default;

    RenderTextureCubeMap::RenderTextureCubeMap(const std::string &image_directory, const std::unordered_map<CubemapSlotName, std::string> &slots)
        : RenderTexture(image_directory), cubemap_slots_(slots)
    {
    }

    RenderTextureCubeMap::RenderTextureCubeMap(const std::string &image_directory, unsigned int handle)
        : RenderTexture(image_directory)
    {
        texture_handle_ = handle;
    }

    bool RenderTextureCubeMap::Initialize()
    {

        glGenTextures(1, &texture_handle_);
        glBindTexture(GL_TEXTURE_CUBE_MAP, texture_handle_);
        stbi_set_flip_vertically_on_load(true);

        int width = 0, height = 0, nr_channels = 0;

        for (int i = 0; i < 6; i++)
        {
            CubemapSlotName slot_name = static_cast<CubemapSlotName>(i);
            if (!cubemap_slots_.contains(slot_name))
            {
                KP_LOG("CubeMapTextureLog", LOG_LEVEL_ERROR, "Couldn't find desired slot");
                return false;
            }
            std::string face_path = GetAssetDirectory() + image_id_ + cubemap_slots_[slot_name];
            unsigned char *image_data = stbi_load(face_path.c_str(), &width, &height, &nr_channels, 0);
            if (image_data)
            {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, image_data);
                stbi_image_free(image_data);
            }
            else
            {
                KP_LOG("CubeMapTextureLog", LOG_LEVEL_ERROR, "Failed to load texture from %s", face_path.c_str());
                stbi_image_free(image_data);
                return false;
            }
        }

        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
        return true;
    }

    RenderTextureCubeMap::~RenderTextureCubeMap() = default;

    RenderTextureHDR::RenderTextureHDR(const std::string &image_directory) : RenderTexture(image_directory)
    {
    }

    bool RenderTextureHDR::Initialize()
    {

        int width = 0, height = 0, nr_channels = 0;
        stbi_set_flip_vertically_on_load(true);

        std::string absoulte_image_path = GetAssetDirectory() + image_id_;
        float *data = stbi_loadf(absoulte_image_path.c_str(), &width, &height, &nr_channels, 0);

        if (!data)
        {
            KP_LOG("TextureLog", LOG_LEVEL_ERROR, "Failed to load hdr_texture from %s", absoulte_image_path.c_str());
            return false;
        }

        glGenTextures(1, &texture_handle_);
        glBindTexture(GL_TEXTURE_2D, texture_handle_);

        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, width, height, 0, GL_RGB, GL_FLOAT, data);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        stbi_image_free(data);
        return true;
    }

    RenderTextureHDR::~RenderTextureHDR() = default;

}