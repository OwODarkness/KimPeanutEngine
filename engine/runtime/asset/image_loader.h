#ifndef KPENGINE_RUNTIME_ASSET_IMAGE_LOADER_H
#define KPENGINE_RUNTIME_ASSET_IMAGE_LOADER_H

#include <string>
#include "asset.h"

namespace kpengine::asset{
    class ImageLoader{
    public:
        virtual bool Load(const std::string& path, AssetRegisterInfo& info) = 0; 
    };
}

#endif