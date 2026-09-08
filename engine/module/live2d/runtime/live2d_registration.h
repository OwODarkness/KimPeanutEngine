#ifndef KPENGINE_LIVE2D_REGISTRATION_H
#define KPENGINE_LIVE2D_REGISTRATION_H

#include <string>

#include "asset/asset_manager.h"

namespace kpengine::live2d
{
    bool RegisterLive2DAssetTypes(asset::AssetManager &manager,
                                  std::string &diagnostic);

}

#endif
