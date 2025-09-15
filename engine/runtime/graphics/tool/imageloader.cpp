#include "imageloader.h"
#include "stb_image/image_helper.h"
#include "log/logger.h"

namespace kpengine::graphics
{
    TextureFormat GetTextureFormatFromChannel(int channel)
    {
        switch (channel)
        {
        case 1:
            return TextureFormat::TEXTURE_FORMAT_R8_UNORM;
        case 2:
            return TextureFormat::TEXTURE_FORMAT_RG8_UNORM;
        case 3:
            return TextureFormat::TEXTURE_FORMAT_RGB8_UNORM;
        case 4:
            return TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM;
        default:
            return TextureFormat::TEXTURE_FORMAT_UNKOWN;
        }
    }

    bool ImageLoader::ReadFromFile(const std::string &path, TextureData &data)
    {
        int w, h, c;
        stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &c, STBI_rgb_alpha);

        if (!pixels)
        {
            KP_LOG("ImageLoaderLog", LOG_LEVEL_ERROR, "Failed to load image from %s", path.c_str());
        }
        data.format = TextureFormat::TEXTURE_FORMAT_RGBA8_UNORM;
        data.width = w;
        data.height = h;
        data.pixels.assign(pixels, pixels + 4 * w * h);
    }
}