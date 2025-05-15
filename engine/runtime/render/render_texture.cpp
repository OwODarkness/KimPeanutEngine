#include "render_texture.h"

#include <filesystem>

#include <stb_image/image_helper.h>
#include <glad/glad.h>

#include "runtime/core/log/logger.h"
#include "platform/path/path.h"

namespace kpengine
{
    RenderTexture::RenderTexture(const std::string &image_relative_path):
    image_id_(image_relative_path)
    {
    }


    RenderTexture::~RenderTexture()
    {
        glDeleteTextures(1, &texture_handle_);
    }

    RenderTexture2D::RenderTexture2D(const std::string &image_path) : RenderTexture(image_path)
    {
    }

    bool RenderTexture2D::Initialize()
    {
        int width = 0, height = 0, nr_channels = 0;
        stbi_set_flip_vertically_on_load(true);

        std::string absoulte_image_path = GetAssetDirectory() + image_id_;

        unsigned char *image_data = stbi_load(absoulte_image_path.c_str(), &width, &height, &nr_channels, 0);
        if (!image_data)
        {
            KP_LOG("TextureLog", LOG_LEVEL_ERROR, "Failed to load texture from %s", absoulte_image_path.c_str());
            return false;
        }
        // else
        // {
        //     KP_LOG("TextureLog", LOG_LEVEL_DISPLAY, "Load texture successfully from %s ", absoulte_image_path.c_str());
        // }
        glGenTextures(1, &texture_handle_);
        glBindTexture(GL_TEXTURE_2D, texture_handle_);

        int color_format = 0;
        if (nr_channels == 1)
        {
            color_format = GL_RED;
        }
        else if (nr_channels == 3)
        {
            color_format = GL_RGB;

        }
        else if (nr_channels == 4)
        {
            color_format = GL_RGBA;
        }

        glTexImage2D(GL_TEXTURE_2D, 0, color_format, width, height, 0, color_format, GL_UNSIGNED_BYTE, image_data);
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(image_data);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        glBindTexture(GL_TEXTURE_2D, 0);

        return true;
    }

    RenderTexture2D::~RenderTexture2D() = default;

    RenderTextureCubeMap::RenderTextureCubeMap(const std::string &image_directory, const std::unordered_map<CubemapSlotName, std::string>& slots)
     : RenderTexture(image_directory),cubemap_slots_(slots)
    {
    }

    RenderTextureCubeMap::RenderTextureCubeMap(const std::string &image_directory, unsigned int handle)
    :RenderTexture(image_directory)
    {
        texture_handle_ = handle;
    }

    bool RenderTextureCubeMap::Initialize()
    {

        glGenTextures(1, &texture_handle_);
        glBindTexture(GL_TEXTURE_CUBE_MAP, texture_handle_);

        int width = 0, height = 0, nr_channels = 0;

        for(int i = 0;i<6;i++)
        {
            CubemapSlotName slot_name = static_cast<CubemapSlotName>(i);
            if(!cubemap_slots_.contains(slot_name))
            {
                KP_LOG("CubeMapTextureLog", LOG_LEVEL_ERROR, "Couldn't find desired slot");
                return false;
            }
            std::string face_path = GetAssetDirectory() + image_id_  + cubemap_slots_[slot_name];
            unsigned char *image_data = stbi_load(face_path.c_str(), &width, &height, &nr_channels, 0);
            if(image_data)
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

    RenderTextureHDR::RenderTextureHDR(const std::string& image_directory):
    RenderTexture(image_directory)
    {

    }

    bool RenderTextureHDR::Initialize()
    {


        int width = 0, height = 0, nr_channels = 0;
        stbi_set_flip_vertically_on_load(true);

        std::string absoulte_image_path = GetAssetDirectory() + image_id_;
        float *data = stbi_loadf(absoulte_image_path.c_str(), &width, &height, &nr_channels, 0);

        if(!data)
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