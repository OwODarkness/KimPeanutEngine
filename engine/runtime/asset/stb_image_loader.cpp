#include "stb_image_loader.h"
#include <magic_enum/magic_enum.hpp>
#include "stb_image/image_helper.h"
#include "log/logger.h"
#include "texture.h"
#include "utility.h"
namespace kpengine::asset{
    bool StbImageLoader::Load(const std::string& path, AssetRegisterInfo& info)
    {
        int width = 0;
        int height = 0;
        int channels = 0;

        stbi_uc* pixles = stbi_load(path.c_str(), &width, &height, &channels, STBI_rgb_alpha);
        if(!pixles)
        {
            KP_LOG("ImageLoaderLog", LOG_LEVEL_ERROR, "Failed to load image from %s", path.c_str());
            return false;
        }
        channels = 4;
        TextureFormat format;
        switch (channels)
        {
        case 1: format = TextureFormat::TEXTURE_FORMAT_R8_SRGB;break;
        case 2: format = TextureFormat::TEXTURE_FORMAT_RG8_SRGB;break;
        case 3: format = TextureFormat::TEXTURE_FORMAT_RGB8_SRGB;break;
        case 4:format = TextureFormat::TEXTURE_FORMAT_RGBA8_SRGB;break;
        default:
            stbi_image_free(pixles);
            KP_LOG("ImageLoaderLog", LOG_LEVEL_ERROR, "Unsupported image %s channel count: %d", path.c_str(), channels);
            return false;
        }
        std::shared_ptr<TextureResource> tex = std::make_shared<TextureResource>();
        size_t total_bytes = static_cast<size_t>(width * height * channels);

        tex->resource->pixels.resize(total_bytes);
        tex->resource->width = width;
        tex->resource->height = height;
        tex->resource->format = format;
        tex->channel_count = channels;
        memcpy(tex->resource->pixels.data(), pixles, total_bytes);
        stbi_image_free(pixles);
        
        info.type = AssetType::KPAT_Texture;
        info.path = path;
        info.name = std::string(magic_enum::enum_name(info.type)) + ExtractNameFromPath(path);
        info.resource = tex;
        return true;
    }
}