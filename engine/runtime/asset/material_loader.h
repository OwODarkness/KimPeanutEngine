#ifndef KPENGINE_RUNTIME_ASSET_MATERIAL_LOADER_H
#define KPENGINE_RUNTIME_ASSET_MATERIAL_LOADER_H

#include <string>

#include "asset.h"

namespace kpengine::asset
{
    class MaterialLoader
    {
    public:
        bool Load(const std::string &path, AssetRegisterInfo &info);
    };
}

#endif
