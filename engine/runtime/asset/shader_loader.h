#ifndef KPENGINE_RUNTIME_ASSET_SHADER_LOADER_H
#define KPENGINE_RUNTIME_ASSET_SHADER_LOADER_H

#include "asset.h"
namespace kpengine::asset{
    class ShaderLoader{
    public:
        bool Load(const std::string& path, AssetRegisterInfo& info);         
    };
}

#endif