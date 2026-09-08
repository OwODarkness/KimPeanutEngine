#ifndef KPENGINE_LIVE2D_ASSET_RUNTIME_H
#define KPENGINE_LIVE2D_ASSET_RUNTIME_H

#include "asset/asset.h"

namespace kpengine::live2d
{
    bool LoadLive2DProduct(const std::string &path, asset::AssetRegisterInfo &info);
}

#endif
