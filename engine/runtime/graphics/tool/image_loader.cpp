#include "image_loader.h"
#include "stb_image/image_helper.h"
#include "log/logger.h"

namespace kpengine::graphics
{


    bool ImageLoader::ReadFromFile(const std::string &path, TextureData &data)
    {
        int w, h, c;
        stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &c, STBI_rgb_alpha);

        if (!pixels)
        {
            KP_LOG("ImageLoaderLog", LOG_LEVEL_ERROR, "Failed to load image from %s", path.c_str());
            return false;
        }
        data.width = w;
        data.height = h;
        data.path = path;
        data.pixels.assign(pixels, pixels + 4 * w * h);
        stbi_image_free(pixels);
        return true;
    }
}