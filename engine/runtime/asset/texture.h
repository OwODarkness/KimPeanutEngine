#ifndef KPENGINE_RUNTIME_ASSET_TEXTURE_RESOURCE_H
#define KPENGINE_RUNTIME_ASSET_TEXTURE_RESOURCE_H


#include <memory>
#include "data/texture.h"


namespace kpengine::asset{
    using TextureData = kpengine::data::TextureData;

    struct TextureResource{
        std::shared_ptr<TextureData> resource;
        uint32_t channel_count;
        TextureResource():resource(std::make_shared<TextureData>()){}
    };
}

#endif