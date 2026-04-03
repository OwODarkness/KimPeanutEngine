#ifndef KPENGINE_RUNTIME_ASSET_STB_IMAGE_LOADER_H
#define KPENGINE_RUNTIME_ASSET_STB_IMAGE_LOADER_H

#include "image_loader.h"

namespace kpengine::asset{
    class StbImageLoader: public ImageLoader{
    public:
        virtual bool Load(const std::string& path, AssetRegisterInfo& info) override; 
    };
}

#endif

