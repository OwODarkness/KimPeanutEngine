#include "render_texture.h"

#include <filesystem>

#include <stb_image/image_helper.h>
#include <glad/glad.h>

#include "runtime/core/log/logger.h"
#include "platform/path/path.h"

namespace kpengine
{
    RenderTexture::RenderTexture(const std::string &image_path) : image_path_(image_path)
    {
    }

    RenderTexture2D::RenderTexture2D(const std::string &image_path) : RenderTexture(image_path)
    {
    }

    void RenderTexture2D::Initialize()
    {
        glGenTextures(1, &texture_handle_);
        glBindTexture(GL_TEXTURE_2D, texture_handle_);

        int width = 0, height = 0, nr_channels = 0;
        stbi_set_flip_vertically_on_load(true);
        unsigned char *image_data = stbi_load(image_path_.c_str(), &width, &height, &nr_channels, 0);
        if (!image_data)
        {
            KP_LOG("TextureLog", LOG_LEVEL_ERROR, "Failed to load texture from %s", image_path_.c_str());
            image_path_ = GetTextureDirectory() + "default.jpg";
            image_data = stbi_load(image_path_.c_str(), &width, &height, &nr_channels, 0);
        }

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
    }

    RenderTextureCubeMap::RenderTextureCubeMap(const std::string &image_directory, const std::vector<std::string>& face_names)
     : RenderTexture(image_directory),
     face_names_(face_names)
    {
    }

    void RenderTextureCubeMap::Initialize()
    {

        glGenTextures(1, &texture_handle_);
        glBindTexture(GL_TEXTURE_CUBE_MAP, texture_handle_);

        int width = 0, height = 0, nr_channels = 0;

        for(int i = 0;i<face_names_.size();i++)
        {
             
            std::string item_path = image_path_ + '/' + face_names_[i];
            unsigned char *image_data = stbi_load(item_path.c_str(), &width, &height, &nr_channels, 0);
            if(image_data)
            {
                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB, width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, image_data);
                stbi_image_free(image_data);
            }
            else
            {
                KP_LOG("TextureLog", LOG_LEVEL_ERROR, "Failed to load texture from %s", item_path.c_str());
                stbi_image_free(image_data);
            }
        }

        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    }
}